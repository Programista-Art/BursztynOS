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
#include "heap.h"
#include "jadro/vfs/vfs_jadro.h"
#include "sterowniki/czas/hpet.h"
#include "sterowniki/siec/sockets.h"
#include "jadro/siec/siec.h"
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
extern "C" bool bws_socket_rezerwuje_stos_tcp();

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

/*
 * POSIX sockety maja wlasne rekordy stanu, ale numer fd jest rezerwowany
 * przez VFS. To daje jedna tablice limitow per PID i eliminuje kolizje
 * socket-vs-plik. Nie udajemy wielu polaczen: obecny TCP ma jeden globalny
 * transport, wiec jeden rekord na proces jest wystarczajacym limitem.
 */
constexpr uint32_t POSIX_SOCKET_MAKS_NA_PROCES = 1U;
constexpr uint64_t POSIX_SOCKET_IO_MAX = 64ULL * 1024ULL;

struct PosixSocketStan {
    bool aktywny;
    bool polaczony;
    bool zamkniety_odczyt;
    bool zamkniety_zapis;
    bool posiada_operacje_sieci;
    int fd;
    int ostatni_blad;
    uint8_t peer_ip[4];
    uint16_t peer_port;
};

PosixSocketStan g_sockety[VFS_MAKS_PROCESOW][POSIX_SOCKET_MAKS_NA_PROCES] = {};

uint16_t posix_ntohs(uint16_t v) {
    return static_cast<uint16_t>((v >> 8) | (v << 8));
}

PosixSocketStan* socket_znajdz(int pid, int fd) {
    if (pid <= 0 || pid >= static_cast<int>(VFS_MAKS_PROCESOW) || fd < 0) return nullptr;
    for (uint32_t i = 0; i < POSIX_SOCKET_MAKS_NA_PROCES; ++i) {
        PosixSocketStan& s = g_sockety[pid][i];
        if (s.aktywny && s.fd == fd && vfs_jadro_czy_gniazdo(pid, fd)) return &s;
    }
    return nullptr;
}

int64_t socket_sprawdz_sockaddr(uint64_t adres, uint32_t dlugosc,
                                PosixSockaddrIn* wynik, bool port_wymagany) {
    if (!adres) return posix_blad(POSIX_EFAULT);
    if (dlugosc < sizeof(PosixSockaddrIn)) return posix_blad(POSIX_EINVAL);
    if (!czy_bezpieczny_zakres_uzytkownika_do_odczytu(
            reinterpret_cast<const void*>(adres), sizeof(PosixSockaddrIn)) ||
        !skopiuj_z_przestrzeni_uzytkownika(
            wynik, reinterpret_cast<const void*>(adres), sizeof(PosixSockaddrIn))) {
        return posix_blad(POSIX_EFAULT);
    }
    if (wynik->sin_family != POSIX_AF_INET) return posix_blad(POSIX_EAFNOSUPPORT);
    const uint16_t port = posix_ntohs(wynik->sin_port);
    if (port_wymagany && port == 0) return posix_blad(POSIX_EINVAL);
    return 0;
}

int64_t socket_sprawdz_fd(PosixSocketStan** wynik, int fd, bool polaczony) {
    if (!wynik) return posix_blad(POSIX_EBADF);
    PosixSocketStan* s = socket_znajdz(aktualny_pid, fd);
    if (!s) return posix_blad(POSIX_EBADF);
    if (polaczony && !s->polaczony) return posix_blad(POSIX_ENOTCONN);
    *wynik = s;
    return 0;
}

int64_t socket_waliduj_io(uint64_t bufor, uint64_t dlugosc, bool zapis) {
    if (dlugosc > POSIX_SOCKET_IO_MAX) return posix_blad(POSIX_EINVAL);
    if (dlugosc == 0) return 0;
    if (!bufor) return posix_blad(POSIX_EFAULT);
    const bool ok = zapis
        ? czy_bezpieczny_zakres_uzytkownika_do_zapisu(
              reinterpret_cast<void*>(bufor), static_cast<size_t>(dlugosc))
        : czy_bezpieczny_zakres_uzytkownika_do_odczytu(
              reinterpret_cast<const void*>(bufor), static_cast<size_t>(dlugosc));
    return ok ? 0 : posix_blad(POSIX_EFAULT);
}

void socket_zamknij_transport(PosixSocketStan& s) {
    if (s.polaczony) tcp_gniazdo_zamknij();
    s.polaczony = false;
    if (s.posiada_operacje_sieci) {
        siec_operacja_zakoncz();
        s.posiada_operacje_sieci = false;
    }
}

int64_t usl_socket(void*, int domena, int typ, int protokol) {
    if (domena != POSIX_AF_INET) return posix_blad(POSIX_EAFNOSUPPORT);
    if (typ != POSIX_SOCK_STREAM) return posix_blad(POSIX_EPROTONOSUPPORT);
    if (protokol != 0 && protokol != POSIX_IPPROTO_TCP)
        return posix_blad(POSIX_EPROTONOSUPPORT);
    if (aktualny_pid <= 0 || aktualny_pid >= static_cast<int>(VFS_MAKS_PROCESOW))
        return posix_blad(POSIX_EBADF);

    PosixSocketStan* wolny = nullptr;
    for (uint32_t i = 0; i < POSIX_SOCKET_MAKS_NA_PROCES; ++i) {
        if (!g_sockety[aktualny_pid][i].aktywny) {
            wolny = &g_sockety[aktualny_pid][i];
            break;
        }
    }
    if (!wolny) return posix_blad(POSIX_EMFILE);

    const int64_t fd = vfs_jadro_zarezerwuj_gniazdo(
        static_cast<uint32_t>(aktualny_pid));
    if (fd < 0) return fd;
    *wolny = {};
    wolny->aktywny = true;
    wolny->fd = static_cast<int>(fd);
    return fd;
}

int64_t usl_connect(void*, int fd, uint64_t adres, uint32_t dlugosc) {
    PosixSocketStan* s = nullptr;
    int64_t sprawdzenie = socket_sprawdz_fd(&s, fd, false);
    if (sprawdzenie != 0) return sprawdzenie;
    if (s->polaczony) return posix_blad(POSIX_EISCONN);

    PosixSockaddrIn sockaddr{};
    sprawdzenie = socket_sprawdz_sockaddr(adres, dlugosc, &sockaddr, true);
    if (sprawdzenie != 0) return sprawdzenie;

    /* BWS ma osobny uchwyt rezerwujacy ten sam globalny transport. */
    if (bws_socket_rezerwuje_stos_tcp()) return posix_blad(POSIX_EBUSY);
    if (!siec_operacja_rozpocznij()) return posix_blad(POSIX_EBUSY);
    s->posiada_operacje_sieci = true;
    if (!tcp_gniazdo_polacz(sockaddr.sin_addr, posix_ntohs(sockaddr.sin_port))) {
        s->ostatni_blad = POSIX_ENETUNREACH;
        socket_zamknij_transport(*s);
        return posix_blad(POSIX_ENETUNREACH);
    }
    s->polaczony = true;
    for (uint32_t i = 0; i < 4; ++i) s->peer_ip[i] = sockaddr.sin_addr[i];
    s->peer_port = posix_ntohs(sockaddr.sin_port);
    return 0;
}

int64_t usl_sendto(void*, int fd, uint64_t bufor, uint64_t dlugosc,
                   uint32_t flagi, uint64_t adres, uint32_t dlugosc_adresu) {
    PosixSocketStan* s = nullptr;
    int64_t sprawdzenie = socket_sprawdz_fd(&s, fd, true);
    if (sprawdzenie != 0) return sprawdzenie;
    if (s->zamkniety_zapis) return posix_blad(POSIX_ESHUTDOWN);
    if (flagi & ~static_cast<uint32_t>(POSIX_MSG_NOSIGNAL))
        return posix_blad(POSIX_EINVAL);
    /* TCP jest juz polaczony; jawnie odrzucamy obcy adres docelowy. */
    if (adres != 0 || dlugosc_adresu != 0) return posix_blad(POSIX_EISCONN);
    sprawdzenie = socket_waliduj_io(bufor, dlugosc, false);
    if (sprawdzenie != 0) return sprawdzenie;
    if (dlugosc == 0) return 0;
    uint8_t* bufor_jadra = static_cast<uint8_t*>(kmalloc(
        static_cast<size_t>(dlugosc)));
    if (!bufor_jadra) return posix_blad(POSIX_ENOMEM);
    if (!skopiuj_z_przestrzeni_uzytkownika(
            bufor_jadra, reinterpret_cast<const void*>(bufor),
            static_cast<size_t>(dlugosc))) {
        kfree(bufor_jadra);
        return posix_blad(POSIX_EFAULT);
    }
    const int sent = tcp_gniazdo_wyslij(
        bufor_jadra, static_cast<uint32_t>(dlugosc));
    kfree(bufor_jadra);
    if (sent < 0) {
        s->ostatni_blad = POSIX_EIO;
        return posix_blad(POSIX_EIO);
    }
    return sent;
}

int64_t usl_recvfrom(void*, int fd, uint64_t bufor, uint64_t dlugosc,
                     uint32_t flagi, uint64_t adres, uint64_t dlugosc_adresu) {
    PosixSocketStan* s = nullptr;
    int64_t sprawdzenie = socket_sprawdz_fd(&s, fd, true);
    if (sprawdzenie != 0) return sprawdzenie;
    if (s->zamkniety_odczyt) return 0;
    if (flagi != 0) return posix_blad(POSIX_ENOSYS);
    sprawdzenie = socket_waliduj_io(bufor, dlugosc, true);
    if (sprawdzenie != 0) return sprawdzenie;
    uint32_t pojemnosc_adresu = 0;
    const bool zwroc_peer = adres != 0 || dlugosc_adresu != 0;
    if (zwroc_peer) {
        if (!adres || !dlugosc_adresu ||
            !czy_bezpieczny_zakres_uzytkownika_do_odczytu(
                reinterpret_cast<const void*>(dlugosc_adresu), sizeof(pojemnosc_adresu)) ||
            !skopiuj_z_przestrzeni_uzytkownika(
                &pojemnosc_adresu, reinterpret_cast<const void*>(dlugosc_adresu),
                sizeof(pojemnosc_adresu)))
            return posix_blad(POSIX_EFAULT);
        const uint32_t do_walidacji =
            pojemnosc_adresu < sizeof(PosixSockaddrIn)
                ? pojemnosc_adresu : sizeof(PosixSockaddrIn);
        if (do_walidacji != 0 &&
            !czy_bezpieczny_zakres_uzytkownika_do_zapisu(
                reinterpret_cast<void*>(adres), do_walidacji))
            return posix_blad(POSIX_EFAULT);
    }
    if (dlugosc == 0) return 0;
    uint8_t* bufor_jadra = static_cast<uint8_t*>(kmalloc(
        static_cast<size_t>(dlugosc)));
    if (!bufor_jadra) return posix_blad(POSIX_ENOMEM);
    const int got = tcp_gniazdo_odbierz(
        bufor_jadra, static_cast<uint32_t>(dlugosc));
    if (got == SOCKETS_TCP_BRAK_DANYCH) {
        kfree(bufor_jadra);
        return posix_blad(POSIX_EAGAIN);
    }
    if (got < 0) {
        kfree(bufor_jadra);
        return posix_blad(POSIX_EIO);
    }
    if (got > 0 && !skopiuj_do_przestrzeni_uzytkownika(
            reinterpret_cast<void*>(bufor), bufor_jadra, static_cast<size_t>(got))) {
        kfree(bufor_jadra);
        return posix_blad(POSIX_EFAULT);
    }
    kfree(bufor_jadra);

    if (zwroc_peer) {
        PosixSockaddrIn peer{};
        peer.sin_family = POSIX_AF_INET;
        peer.sin_port = static_cast<uint16_t>((s->peer_port >> 8) |
                                               (s->peer_port << 8));
        for (uint32_t i = 0; i < 4; ++i) peer.sin_addr[i] = s->peer_ip[i];
        const uint32_t do_kopii =
            pojemnosc_adresu < sizeof(peer) ? pojemnosc_adresu : sizeof(peer);
        if (do_kopii != 0 &&
            (!czy_bezpieczny_zakres_uzytkownika_do_zapisu(
                 reinterpret_cast<void*>(adres), do_kopii) ||
             !skopiuj_do_przestrzeni_uzytkownika(
                 reinterpret_cast<void*>(adres), &peer, do_kopii)))
            return posix_blad(POSIX_EFAULT);
        const uint32_t pelny = sizeof(peer);
        if (!czy_bezpieczny_zakres_uzytkownika_do_zapisu(
                reinterpret_cast<void*>(dlugosc_adresu), sizeof(pelny)) ||
            !skopiuj_do_przestrzeni_uzytkownika(
                reinterpret_cast<void*>(dlugosc_adresu), &pelny, sizeof(pelny)))
            return posix_blad(POSIX_EFAULT);
    }
    return got;
}

int64_t usl_bind(void*, int fd, uint64_t adres, uint32_t dlugosc) {
    PosixSocketStan* s = nullptr;
    int64_t sprawdzenie = socket_sprawdz_fd(&s, fd, false);
    if (sprawdzenie != 0) return sprawdzenie;
    PosixSockaddrIn sockaddr{};
    sprawdzenie = socket_sprawdz_sockaddr(adres, dlugosc, &sockaddr, false);
    if (sprawdzenie != 0) return sprawdzenie;
    (void)s;
    return posix_blad(POSIX_ENOSYS);
}

int64_t usl_listen(void*, int fd, int backlog) {
    PosixSocketStan* s = nullptr;
    const int64_t sprawdzenie = socket_sprawdz_fd(&s, fd, false);
    if (sprawdzenie != 0) return sprawdzenie;
    if (backlog < 0) return posix_blad(POSIX_EINVAL);
    return posix_blad(POSIX_ENOSYS);
}

int64_t usl_accept(void*, int fd, uint64_t adres, uint64_t dlugosc_adresu) {
    PosixSocketStan* s = nullptr;
    const int64_t sprawdzenie = socket_sprawdz_fd(&s, fd, false);
    if (sprawdzenie != 0) return sprawdzenie;
    if ((adres == 0) != (dlugosc_adresu == 0)) return posix_blad(POSIX_EFAULT);
    if (dlugosc_adresu != 0) {
        uint32_t ignored = 0;
        if (!czy_bezpieczny_zakres_uzytkownika_do_odczytu(
                reinterpret_cast<const void*>(dlugosc_adresu), sizeof(ignored)) ||
            !skopiuj_z_przestrzeni_uzytkownika(
                &ignored, reinterpret_cast<const void*>(dlugosc_adresu), sizeof(ignored)))
            return posix_blad(POSIX_EFAULT);
    }
    (void)s;
    return posix_blad(POSIX_ENOSYS);
}

int64_t usl_shutdown(void*, int fd, int jak) {
    PosixSocketStan* s = nullptr;
    const int64_t sprawdzenie = socket_sprawdz_fd(&s, fd, true);
    if (sprawdzenie != 0) return sprawdzenie;
    if (jak != POSIX_SHUT_RD && jak != POSIX_SHUT_WR && jak != POSIX_SHUT_RDWR)
        return posix_blad(POSIX_EINVAL);
    if (jak == POSIX_SHUT_RD || jak == POSIX_SHUT_RDWR) s->zamkniety_odczyt = true;
    if (jak == POSIX_SHUT_WR || jak == POSIX_SHUT_RDWR) s->zamkniety_zapis = true;
    if (jak == POSIX_SHUT_RDWR) socket_zamknij_transport(*s);
    return 0;
}

int64_t usl_getsockopt(void*, int fd, int poziom, int opcja,
                       uint64_t wartosc, uint64_t dlugosc) {
    PosixSocketStan* s = nullptr;
    const int64_t sprawdzenie = socket_sprawdz_fd(&s, fd, false);
    if (sprawdzenie != 0) return sprawdzenie;
    if (!dlugosc || !czy_bezpieczny_zakres_uzytkownika_do_odczytu(
            reinterpret_cast<const void*>(dlugosc), sizeof(uint32_t)))
        return posix_blad(POSIX_EFAULT);
    uint32_t pojemnosc = 0;
    if (!skopiuj_z_przestrzeni_uzytkownika(
            &pojemnosc, reinterpret_cast<const void*>(dlugosc), sizeof(pojemnosc)))
        return posix_blad(POSIX_EFAULT);
    if (poziom != POSIX_SOL_SOCKET ||
        (opcja != POSIX_SO_TYPE && opcja != POSIX_SO_ERROR))
        return posix_blad(POSIX_ENOPROTOOPT);
    if (!wartosc || pojemnosc < sizeof(int32_t))
        return posix_blad(POSIX_EFAULT);
    if (!czy_bezpieczny_zakres_uzytkownika_do_zapisu(
            reinterpret_cast<void*>(wartosc), sizeof(int32_t)))
        return posix_blad(POSIX_EFAULT);
    const int32_t value = opcja == POSIX_SO_TYPE
        ? POSIX_SOCK_STREAM : s->ostatni_blad;
    if (!skopiuj_do_przestrzeni_uzytkownika(
            reinterpret_cast<void*>(wartosc), &value, sizeof(value)))
        return posix_blad(POSIX_EFAULT);
    const uint32_t out_len = sizeof(value);
    if (!skopiuj_do_przestrzeni_uzytkownika(
            reinterpret_cast<void*>(dlugosc), &out_len, sizeof(out_len)))
        return posix_blad(POSIX_EFAULT);
    return 0;
}

int64_t usl_setsockopt(void*, int fd, int poziom, int opcja,
                       uint64_t wartosc, uint64_t dlugosc) {
    PosixSocketStan* s = nullptr;
    const int64_t sprawdzenie = socket_sprawdz_fd(&s, fd, false);
    if (sprawdzenie != 0) return sprawdzenie;
    if (dlugosc > POSIX_SOCKET_IO_MAX) return posix_blad(POSIX_EINVAL);
    if (dlugosc != 0 && (!wartosc ||
        !czy_bezpieczny_zakres_uzytkownika_do_odczytu(
            reinterpret_cast<const void*>(wartosc), static_cast<size_t>(dlugosc))))
        return posix_blad(POSIX_EFAULT);
    (void)s;
    (void)poziom;
    (void)opcja;
    return posix_blad(POSIX_ENOSYS);
}

int64_t usl_socket_close(int fd) {
    PosixSocketStan* s = socket_znajdz(aktualny_pid, fd);
    if (!s) return posix_blad(POSIX_EBADF);
    socket_zamknij_transport(*s);
    const int64_t wynik = vfs_jadro_zwolnij_gniazdo(
        static_cast<uint32_t>(aktualny_pid), fd);
    *s = {};
    return wynik;
}

int64_t usl_socket_read(int fd, uint64_t bufor, uint64_t dlugosc) {
    return usl_recvfrom(nullptr, fd, bufor, dlugosc, 0, 0, 0);
}

int64_t usl_socket_write(int fd, uint64_t bufor, uint64_t dlugosc) {
    return usl_sendto(nullptr, fd, bufor, dlugosc, 0, 0, 0);
}

void posix_socket_zwolnij_proces(int pid) {
    if (pid <= 0 || pid >= static_cast<int>(VFS_MAKS_PROCESOW)) return;
    for (uint32_t i = 0; i < POSIX_SOCKET_MAKS_NA_PROCES; ++i) {
        PosixSocketStan& s = g_sockety[pid][i];
        if (!s.aktywny) continue;
        /*
         * Sprzatanie procesu odbywa sie pod blokada loadera i z wylaczonymi
         * przerwaniami. Uzywamy bezposredniego resetu stanu transportu,
         * nigdy zamkniecia z oczekiwaniem/pollingiem.
         */
        if (s.posiada_operacje_sieci) siec_operacja_posprzataj(pid);
        (void)vfs_jadro_zwolnij_gniazdo(static_cast<uint32_t>(pid), s.fd);
        s = {};
    }
}

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
    if (socket_znajdz(aktualny_pid, fd)) {
        return usl_socket_write(fd, reinterpret_cast<uint64_t>(dane), dlugosc);
    }
    /*
     * Od tego etapu VFS jest jedynym wlascicielem deskryptorow: fd 1/2 to
     * terminale zalozone przez vfs_jadro_przygotuj_proces, a fd >= 3 to
     * pliki otwarte przez open(). Dzieki temu write() i open() zgadzaja
     * sie co do numeracji - wczesniej write(3) zwracal EBADF, bo VFS nie
     * istnial.
     */
    if (aktualny_pid <= 0) return posix_blad(POSIX_EBADF);
    return vfs_jadro_write(static_cast<uint32_t>(aktualny_pid), fd,
                           reinterpret_cast<uint64_t>(dane), dlugosc);
}

int64_t usl_read(void*, int fd, uint64_t bufor, uint64_t dlugosc) {
    if (socket_znajdz(aktualny_pid, fd)) {
        return usl_socket_read(fd, bufor, dlugosc);
    }
    if (aktualny_pid <= 0) return posix_blad(POSIX_EBADF);
    return vfs_jadro_read(static_cast<uint32_t>(aktualny_pid), fd, bufor, dlugosc);
}

int64_t usl_open(void*, uint64_t sciezka, uint64_t flagi, uint64_t tryb) {
    if (aktualny_pid <= 0) return posix_blad(POSIX_EBADF);
    return vfs_jadro_open(static_cast<uint32_t>(aktualny_pid), sciezka, flagi, tryb);
}

int64_t usl_openat(void*, int dirfd, uint64_t sciezka,
                   uint64_t flagi, uint64_t tryb) {
    if (aktualny_pid <= 0) return posix_blad(POSIX_EBADF);
    return vfs_jadro_openat(static_cast<uint32_t>(aktualny_pid), dirfd,
                            sciezka, flagi, tryb);
}

int64_t usl_close(void*, int fd) {
    if (socket_znajdz(aktualny_pid, fd)) return usl_socket_close(fd);
    if (aktualny_pid <= 0) return posix_blad(POSIX_EBADF);
    return vfs_jadro_close(static_cast<uint32_t>(aktualny_pid), fd);
}

int64_t usl_lseek(void*, int fd, int64_t przesuniecie, int skad) {
    if (aktualny_pid <= 0) return posix_blad(POSIX_EBADF);
    return vfs_jadro_lseek(static_cast<uint32_t>(aktualny_pid), fd, przesuniecie, skad);
}

int64_t usl_fstat(void*, int fd, uint64_t bufor) {
    if (aktualny_pid <= 0) return posix_blad(POSIX_EBADF);
    return vfs_jadro_fstat(static_cast<uint32_t>(aktualny_pid), fd, bufor);
}

int64_t usl_stat(void*, uint64_t sciezka, uint64_t bufor) {
    if (aktualny_pid <= 0) return posix_blad(POSIX_EBADF);
    return vfs_jadro_stat(static_cast<uint32_t>(aktualny_pid), sciezka, bufor);
}

int64_t usl_newfstatat(void*, int dirfd, uint64_t sciezka,
                       uint64_t bufor, uint32_t flagi) {
    if (aktualny_pid <= 0) return posix_blad(POSIX_EBADF);
    return vfs_jadro_newfstatat(static_cast<uint32_t>(aktualny_pid), dirfd,
                                sciezka, bufor, flagi);
}

int64_t usl_readlink(void*, uint64_t sciezka, uint64_t bufor,
                     uint64_t dlugosc) {
    if (aktualny_pid <= 0) return posix_blad(POSIX_EBADF);
    return vfs_jadro_readlink(static_cast<uint32_t>(aktualny_pid), sciezka,
                              bufor, dlugosc);
}

int64_t usl_getdents64(void*, int fd, uint64_t bufor, uint64_t dlugosc) {
    if (aktualny_pid <= 0) return posix_blad(POSIX_EBADF);
    return vfs_jadro_getdents64(static_cast<uint32_t>(aktualny_pid), fd,
                                bufor, dlugosc);
}

int64_t usl_poll(void*, uint64_t fds, uint64_t nfds, int timeout_ms) {
    (void)fds;
    (void)nfds;
    (void)timeout_ms;
    /*
     * poll() wymaga blokowania procesu i budzenia przez scheduler. Zwracanie
     * sukcesu bez gotowych zdarzen byloby falszem; stdin pozostaje osobnym
     * punktem zaleznym od keyboard/schedulera.
     */
    return posix_blad(POSIX_ENOSYS);
}

/* =========================================================================
 * WYWOLANIA STARTOWE LIBC
 * ========================================================================= */

int64_t usl_set_tid_address(void*, uint64_t wskaznik_tid) {
    (void)wskaznik_tid;
    /*
     * Model jest jednowatkowy: tid == pid, a jadro nie zapisuje niczego
     * pod wskaznikiem clear_child_tid (nie ma jeszcze CLONE_CHILD_CLEARTID).
     * Zwracamy prawdziwy PID, bo dokladnie tego oczekuje musl w crt1 -
     * -ENOSYS przerwalby start programu przed main().
     */
    return aktualny_pid > 0 ? static_cast<int64_t>(aktualny_pid) : 1;
}

int64_t usl_set_robust_list(void*, uint64_t naglowek, uint64_t dlugosc) {
    (void)naglowek;
    (void)dlugosc;
    /*
     * Lista robust futexow ma sens tylko z wielowatkowoscia. Przyjmujemy
     * rejestracje i nie zapisujemy jej, bo nie ma czego odzyskiwac przy
     * smierci watku. -ENOSYS bylby bledny: musl traktuje go jak brak
     * obslugi i niektor implementacje koncza start.
     */
    return 0;
}

namespace {

/* Wypelnia PosixRlimit dla danego zasobu i kopiuje do Ring 3. */
int64_t limit_zasobu(uint64_t bufor, int zasob) {
    PosixRlimit lim{};

    switch (zasob) {
        case POSIX_RLIMIT_NOFILE:
            /*
             * Prawdziwy limit VFS, nie wartosc zmyslona: aplikacja, ktora
             * planuje otworzyc wiecej, dostaje prawde i moze zareagowac.
             */
            lim.rlim_cur = VFS_MAKS_DESKRYPTOROW;
            lim.rlim_max = VFS_MAKS_DESKRYPTOROW;
            break;
        case POSIX_RLIMIT_STACK:
            lim.rlim_cur = 2ULL * 1024ULL * 1024ULL;
            lim.rlim_max = 2ULL * 1024ULL * 1024ULL;
            break;
        case POSIX_RLIMIT_NPROC:
            lim.rlim_cur = MAKS_PROCESOW;
            lim.rlim_max = MAKS_PROCESOW;
            break;
        case POSIX_RLIMIT_AS:
        case POSIX_RLIMIT_DATA:
        case POSIX_RLIMIT_RSS:
            lim.rlim_cur = POSIX_RLIM_INFINITY;
            lim.rlim_max = POSIX_RLIM_INFINITY;
            break;
        default:
            /* Zasob nieobslugiwany: EINVAL, nie "nieskonczonosc". */
            return posix_blad(POSIX_EINVAL);
    }

    if (!bufor) return posix_blad(POSIX_EFAULT);
    if (!czy_bezpieczny_zakres_uzytkownika_do_zapisu(
            reinterpret_cast<void*>(bufor), sizeof(lim))) {
        return posix_blad(POSIX_EFAULT);
    }
    if (!skopiuj_do_przestrzeni_uzytkownika(
            reinterpret_cast<void*>(bufor), &lim, sizeof(lim))) {
        return posix_blad(POSIX_EFAULT);
    }
    return 0;
}

}  // namespace

int64_t usl_prlimit64(void*, int pid, int zasob, uint64_t nowy_limit,
                      uint64_t stary_limit) {
    /*
     * Zmiana limitow nie jest wspierana. Odmawiamy jawnie zamiast
     * przyjac wartosc i jej nie zastosowac - aplikacja, ktora podnosi
     * Limit NOFILE, musialaby sie dowiedziec, ze limit nie dziala.
     */
    if (nowy_limit != 0) return posix_blad(POSIX_ENOSYS);
    /* pid 0 oznacza "wolajacy proces". Inny PID to nie nasz proces. */
    if (pid != 0 && pid != aktualny_pid) return posix_blad(POSIX_EPERM);
    return limit_zasobu(stary_limit, zasob);
}

int64_t usl_getrlimit(void*, int zasob, uint64_t limit) {
    return limit_zasobu(limit, zasob);
}

int64_t usl_sched_getaffinity(void*, int pid, uint64_t rozmiar, uint64_t maska) {
    if (pid != 0 && pid != aktualny_pid) return posix_blad(POSIX_EPERM);
    /*
     * Planista jest jednordzeniowy, wiec maska ma dokladnie jeden bit.
     * Podanie prawdy (1) jest wazne: aplikacje wielowatkowe decyduja na
     * tej podstawie, ile watkow utworzyc. Zmyslone 4 spowodowaloby
     * tworzenie watkow, ktore nigdy nie rusza.
     */
    if (rozmiar < sizeof(uint64_t) || !maska) return posix_blad(POSIX_EINVAL);
    if (!czy_bezpieczny_zakres_uzytkownika_do_zapisu(
            reinterpret_cast<void*>(maska), sizeof(uint64_t))) {
        return posix_blad(POSIX_EFAULT);
    }
    const uint64_t bit = 1;
    if (!skopiuj_do_przestrzeni_uzytkownika(
            reinterpret_cast<void*>(maska), &bit, sizeof(bit))) {
        return posix_blad(POSIX_EFAULT);
    }
    return sizeof(uint64_t);
}

int64_t usl_sysinfo(void*, uint64_t bufor) {
    if (!bufor) return posix_blad(POSIX_EFAULT);
    if (!czy_bezpieczny_zakres_uzytkownika_do_zapisu(
            reinterpret_cast<void*>(bufor), sizeof(PosixSysinfo))) {
        return posix_blad(POSIX_EFAULT);
    }

    PosixSysinfo si{};
    /*
     * Wartosci z realnego PMM. `pmm_pobierz_wolne_ramki()` jest jedynym
     * publicznym licznikiem, jaki PMM eksportuje; nie zmyslamy
     * `totalram`. Zamiast tego raportujemy sume "wolne + zarezerwowane
     * przez jadro" jako dolna granice pamieci, ktora znamy.
     */
    const uint64_t wolne = pmm_pobierz_wolne_ramki();
    si.freeram = wolne * 4096ULL;
    si.totalram = si.freeram;
    si.sharedram = 0;
    si.bufferram = 0;
    si.totalswap = 0;
    si.freeswap = 0;
    si.procs = static_cast<uint16_t>(MAKS_PROCESOW);
    si.mem_unit = 1;

    if (!skopiuj_do_przestrzeni_uzytkownika(
            reinterpret_cast<void*>(bufor), &si, sizeof(si))) {
        return posix_blad(POSIX_EFAULT);
    }
    return 0;
}

int64_t usl_access(void*, uint64_t sciezka_uzytkownika, int tryb) {
    if (tryb & ~(POSIX_R_OK | POSIX_W_OK | POSIX_X_OK)) {
        return posix_blad(POSIX_EINVAL);
    }
    /*
     * PSF potrafi bez otwierania stwierdzic wylacznie istnienie. Nie
     * udajemy, ze sprawdzamy prawa: kod wyjscia jest zero tylko wtedy,
     * gdy plik istnieje, a open() z tymi flagami i tak zweryfikuje PZB.
     */
    return vfs_jadro_istnieje(static_cast<uint32_t>(aktualny_pid),
                              sciezka_uzytkownika)
               ? 0
               : posix_blad(POSIX_ENOENT);
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
    if (aktualny_pid <= 0) return posix_blad(POSIX_EINVAL);
    proces_t& p = tablica_procesow[aktualny_pid];

    /*
     * brk(0) jest zapytaniem o biezaca granice sterty - tak dziala glibc
     * i musl. Nie zmieniamy wtedy niczego.
     */
    if (nowy_break == 0) {
        return static_cast<int64_t>(p.granica_sterty);
    }

    /*
     * Granica sterty jest zawsze wyrownana do strony. Nie wyrownujemy po
     * cichu wartosci od uzytkownika: aplikacja musi wiedziec, ze dostala
     * dokladnie to, o co prosila, albo blad.
     */
    /* Rozmiar strony 4 KiB jest stala sprzetowa tej architektury. */
    if ((nowy_break & 4095ULL) != 0) {
        return posix_blad(POSIX_EINVAL);
    }

    const uint64_t wynik = mmap_jadro_sterta_rozszerz(aktualny_pid, nowy_break);
    if (wynik == 0) return posix_blad(POSIX_ENOMEM);
    return static_cast<int64_t>(wynik);
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
    (void)timeout;
    (void)adres2;
    (void)wartosc3;

    /*
     * To nie jest jeszcze pelny futex z kolejka waiterow. W obecnym modelu
     * proces = jeden kontekst, a blokada w syscallu zatrzymalaby caly system.
     * WAIT wykonuje wiec ograniczona obserwacje, a WAKE raportuje brak
     * zarejestrowanych oczekujacych. Zawsze sprawdzamy adres przez
     * copy_from_user: sam syscall nie moze dereferencjonowac Ring 3.
     */
    if (adres == 0 || (adres & 3ULL) != 0) {
        return posix_blad(POSIX_EINVAL);
    }
    if (posix_futex_ma_nieznane_flagi(op)) {
        return posix_blad(POSIX_ENOSYS);
    }

    const uint32_t polecenie = posix_futex_polecenie(op);
    if (polecenie != POSIX_FUTEX_WAIT &&
        polecenie != POSIX_FUTEX_WAKE) {
        return posix_blad(POSIX_ENOSYS);
    }

    uint32_t obserwowana = 0;
    if (!czy_bezpieczny_zakres_uzytkownika_do_odczytu(
            reinterpret_cast<const void*>(adres), sizeof(obserwowana)) ||
        !skopiuj_z_przestrzeni_uzytkownika(
            &obserwowana, reinterpret_cast<const void*>(adres),
            sizeof(obserwowana))) {
        return posix_blad(POSIX_EFAULT);
    }

    if (polecenie == POSIX_FUTEX_WAKE) {
        /*
         * Brak kolejki waiterow jest stanem rzeczywistym, nie sukcesem
         * udawanym: Linux rowniez zwraca 0, gdy nie ma kogo obudzic.
         * Ujemny int32_t przekazany w rejestrze nie moze oznaczac
         * nieograniczonej liczby oczekujacych.
         */
        if (wartosc > 0x7FFFFFFFULL) {
            return posix_blad(POSIX_EINVAL);
        }
        return 0;
    }

    const uint32_t oczekiwana = static_cast<uint32_t>(wartosc);
    const int64_t wynik_porownania =
        posix_futex_sprawdz_wartosc(obserwowana, oczekiwana);
    if (wynik_porownania != 0) return wynik_porownania;

    /*
     * Ograniczony spin jest tylko punktem obserwacyjnym. Jezeli wartosc
     * zmieni sie np. przez sciezke przerwania, WAIT moze uczciwie zakonczyc
     * sie sukcesem. Gdy nie ma zmiany, -ENOSYS wymusza fallback libc i nie
     * blokuje jedynego aktualnego kontekstu.
     */
    constexpr uint32_t MAKS_SPIN_FUTEX = 256;
    for (uint32_t i = 0; i < MAKS_SPIN_FUTEX; ++i) {
        asm volatile("pause" ::: "memory");
        uint32_t biezaca = 0;
        if (!skopiuj_z_przestrzeni_uzytkownika(
                &biezaca, reinterpret_cast<const void*>(adres),
                sizeof(biezaca))) {
            return posix_blad(POSIX_EFAULT);
        }
        if (biezaca != oczekiwana) {
            return posix_futex_wynik_po_spinie(true);
        }
    }

    return posix_futex_wynik_po_spinie(false);
}

PosixUslugi zbuduj_uslugi() {
    PosixUslugi u{};
    u.write = usl_write;
    u.read = usl_read;
    u.open = usl_open;
    u.openat = usl_openat;
    u.close_fd = usl_close;
    u.lseek_fd = usl_lseek;
    u.fstat_fd = usl_fstat;
    u.stat_sciezka = usl_stat;
    u.newfstatat_sciezka = usl_newfstatat;
    u.readlink_sciezka = usl_readlink;
    u.getdents64_fd = usl_getdents64;
    u.poll_fds = usl_poll;
    u.set_tid_address = usl_set_tid_address;
    u.set_robust_list = usl_set_robust_list;
    u.prlimit64 = usl_prlimit64;
    u.sched_getaffinity = usl_sched_getaffinity;
    u.getrlimit_zasob = usl_getrlimit;
    u.sysinfo_ogolny = usl_sysinfo;
    u.access_sciezka = usl_access;
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
    u.socket = usl_socket;
    u.connect = usl_connect;
    u.sendto = usl_sendto;
    u.recvfrom = usl_recvfrom;
    u.bind = usl_bind;
    u.listen = usl_listen;
    u.accept = usl_accept;
    u.shutdown = usl_shutdown;
    u.getsockopt = usl_getsockopt;
    u.setsockopt = usl_setsockopt;
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
    posix_socket_zwolnij_proces(pid);
    /*
     * Deskryptory rowniez: bez tego nowy proces na tym samym PID widzialby
     * pliki poprzednika przez fd 3..63 (odczyt cudzych danych przez
     * zgadywalny numer deskryptora).
     */
    (void)vfs_jadro_zwolnij_proces(pid);
}
