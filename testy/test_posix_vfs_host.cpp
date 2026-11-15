/*
 * Host gate POSIX/VFS (bez PSF i bez QEMU).
 *
 * Sprawdza czysta budowe linux_dirent64 oraz mapowanie argumentow nowych
 * wywolan. Sciezki Ring 3 sa reprezentowane przez liczby - test nie
 * dereferencjonuje ich, a stuby zwracaja bledy dla nieobslugiwanych dirfd.
 */
#include "jadro/posix/posix_dyspozytor.h"
#include "jadro/vfs/vfs.h"

#include <cstdint>
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

struct Zapisane {
    int openat_dirfd = 0;
    uint64_t openat_path = 0;
    uint64_t openat_flags = 0;
    uint64_t openat_mode = 0;
    int statat_dirfd = 0;
    uint64_t statat_path = 0;
    uint64_t statat_buf = 0;
    uint32_t statat_flags = 0;
    uint64_t readlink_path = 0;
    uint64_t readlink_buf = 0;
    uint64_t readlink_len = 0;
    int getdents_fd = 0;
    uint64_t getdents_buf = 0;
    uint64_t getdents_len = 0;
    uint64_t poll_fds = 0;
    uint64_t poll_nfds = 0;
    int poll_timeout = 0;
};

Zapisane g_z{};

int64_t openat_stub(void*, int dirfd, uint64_t path, uint64_t flags,
                    uint64_t mode) {
    g_z.openat_dirfd = dirfd;
    g_z.openat_path = path;
    g_z.openat_flags = flags;
    g_z.openat_mode = mode;
    return dirfd == POSIX_AT_FDCWD ? 17 : posix_blad(POSIX_EBADF);
}

int64_t statat_stub(void*, int dirfd, uint64_t path, uint64_t buf,
                    uint32_t flags) {
    g_z.statat_dirfd = dirfd;
    g_z.statat_path = path;
    g_z.statat_buf = buf;
    g_z.statat_flags = flags;
    return dirfd == POSIX_AT_FDCWD ? 0 : posix_blad(POSIX_EBADF);
}

int64_t readlink_stub(void*, uint64_t path, uint64_t buf, uint64_t len) {
    g_z.readlink_path = path;
    g_z.readlink_buf = buf;
    g_z.readlink_len = len;
    return posix_blad(POSIX_EINVAL);
}

int64_t getdents_stub(void*, int fd, uint64_t buf, uint64_t len) {
    g_z.getdents_fd = fd;
    g_z.getdents_buf = buf;
    g_z.getdents_len = len;
    return fd < 0 ? posix_blad(POSIX_EBADF) : 0;
}

int64_t poll_stub(void*, uint64_t fds, uint64_t nfds, int timeout) {
    g_z.poll_fds = fds;
    g_z.poll_nfds = nfds;
    g_z.poll_timeout = timeout;
    return posix_blad(POSIX_ENOSYS);
}

PosixUslugi uslugi() {
    PosixUslugi u{};
    u.openat = openat_stub;
    u.newfstatat_sciezka = statat_stub;
    u.readlink_sciezka = readlink_stub;
    u.getdents64_fd = getdents_stub;
    u.poll_fds = poll_stub;
    return u;
}

PosixRama rama(uint64_t numer, uint64_t a1 = 0, uint64_t a2 = 0,
               uint64_t a3 = 0, uint64_t a4 = 0) {
    PosixRama r{};
    r.numer = numer;
    r.a1 = a1;
    r.a2 = a2;
    r.a3 = a3;
    r.a4 = a4;
    return r;
}

uint16_t odczytaj_u16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0]) |
           static_cast<uint16_t>(static_cast<uint16_t>(p[1]) << 8);
}

uint64_t odczytaj_u64(const uint8_t* p) {
    uint64_t wynik = 0;
    for (unsigned i = 0; i < 8; ++i) wynik |= uint64_t(p[i]) << (8U * i);
    return wynik;
}

void test_dirent_builder() {
    uint8_t bufor[128]{};
    size_t zapisano = 0;
    check(vfs_zbuduj_linux_dirent64(
              bufor, sizeof(bufor), 123, 9, VFS_DIRENT_PLIK,
              "plik.py", 8, &zapisano),
          "dirent: poprawny rekord");
    check(zapisano == 32, "dirent: rekord wyrównany do 8");
    check(odczytaj_u64(bufor) == 123, "dirent: inode");
    check(odczytaj_u64(bufor + 8) == 9, "dirent: d_off");
    check(odczytaj_u16(bufor + 16) == zapisano, "dirent: d_reclen");
    check(bufor[18] == VFS_DIRENT_PLIK, "dirent: d_type");
    check(std::strcmp(reinterpret_cast<const char*>(bufor + 19), "plik.py") == 0,
          "dirent: nazwa");

    size_t drugi = 0;
    check(vfs_zbuduj_linux_dirent64(
              bufor + zapisano, sizeof(bufor) - zapisano, 456, 10,
              VFS_DIRENT_KATALOG, "katalog", 8, &drugi),
          "dirent: drugi rekord");
    check(drugi == 32, "dirent: drugi rozmiar");
    check(odczytaj_u64(bufor + zapisano + 8) == 10,
          "dirent: offset nastepnego wpisu");
    check(bufor[zapisano + 18] == VFS_DIRENT_KATALOG,
          "dirent: typ katalogu");
}

void test_dirent_bledy() {
    uint8_t bufor[32]{};
    size_t zapisano = 99;
    check(!vfs_zbuduj_linux_dirent64(
              nullptr, sizeof(bufor), 1, 1, VFS_DIRENT_PLIK,
              "x", 2, &zapisano),
          "dirent: nullptr bufora");
    check(zapisano == 0, "dirent: nullptr nie zapisuje wyniku");
    check(!vfs_zbuduj_linux_dirent64(
              bufor, sizeof(bufor), 1, 1, VFS_DIRENT_PLIK,
              nullptr, 2, &zapisano),
          "dirent: nullptr nazwy");
    check(!vfs_zbuduj_linux_dirent64(
              bufor, sizeof(bufor), 1, 1, VFS_DIRENT_PLIK,
              "brak-nula", 3, &zapisano),
          "dirent: nazwa poza limitem");
    check(!vfs_zbuduj_linux_dirent64(
              bufor, 8, 1, 1, VFS_DIRENT_PLIK,
              "x", 2, &zapisano),
          "dirent: za maly bufor");
    check(!vfs_zbuduj_linux_dirent64(
              bufor, sizeof(bufor), 1, UINT64_C(0x8000000000000000),
              VFS_DIRENT_PLIK, "x", 2, &zapisano),
          "dirent: d_off poza signed long");
}

void test_dispatch_mapping() {
    const PosixUslugi u = uslugi();
    PosixDiagnostyka d{};

    g_z = Zapisane{};
    check(posix_obsluz(u, rama(POSIX_SYS_openat, POSIX_AT_FDCWD, 0x1000,
                               VFS_O_RDONLY, 0644), &d) == 17,
          "openat: callback");
    check(g_z.openat_dirfd == POSIX_AT_FDCWD &&
              g_z.openat_path == 0x1000 &&
              g_z.openat_flags == VFS_O_RDONLY &&
              g_z.openat_mode == 0644,
          "openat: RDI/RSI/RDX/R10");

    g_z = Zapisane{};
    check(posix_obsluz(u, rama(POSIX_SYS_newfstatat, POSIX_AT_FDCWD,
                               0x2000, 0x3000, POSIX_AT_SYMLINK_NOFOLLOW),
                       &d) == 0,
          "newfstatat: callback");
    check(g_z.statat_dirfd == POSIX_AT_FDCWD &&
              g_z.statat_path == 0x2000 &&
              g_z.statat_buf == 0x3000 &&
              g_z.statat_flags == POSIX_AT_SYMLINK_NOFOLLOW,
          "newfstatat: argumenty");

    check(posix_obsluz(u, rama(POSIX_SYS_openat, -5, 0x1000), &d) ==
              posix_blad(POSIX_EBADF),
          "openat: nieobslugiwany dirfd -> EBADF");
    check(posix_obsluz(u, rama(POSIX_SYS_newfstatat, -5, 0x1000), &d) ==
              posix_blad(POSIX_EBADF),
          "newfstatat: nieobslugiwany dirfd -> EBADF");

    g_z = Zapisane{};
    check(posix_obsluz(u, rama(POSIX_SYS_readlink, 0x4000, 0x5000, 55), &d) ==
              posix_blad(POSIX_EINVAL),
          "readlink: jawny brak symlinkow");
    check(g_z.readlink_path == 0x4000 && g_z.readlink_buf == 0x5000 &&
              g_z.readlink_len == 55,
          "readlink: argumenty");

    g_z = Zapisane{};
    check(posix_obsluz(u, rama(POSIX_SYS_getdents64, 7, 0x6000, 4096), &d) == 0,
          "getdents64: callback");
    check(g_z.getdents_fd == 7 && g_z.getdents_buf == 0x6000 &&
              g_z.getdents_len == 4096,
          "getdents64: argumenty");
    check(posix_obsluz(u, rama(POSIX_SYS_getdents64, UINT64_MAX, 0x6000, 1),
                       &d) == posix_blad(POSIX_EBADF),
          "getdents64: fd -1 -> EBADF");

    g_z = Zapisane{};
    check(posix_obsluz(u, rama(POSIX_SYS_poll, 0x7000, 2,
                               static_cast<uint64_t>(-1)), &d) ==
              posix_blad(POSIX_ENOSYS),
          "poll: scheduler jawnie nieobslugiwany");
    check(g_z.poll_fds == 0x7000 && g_z.poll_nfds == 2 &&
              g_z.poll_timeout == -1,
          "poll: argumenty");
}

}  // namespace

int main() {
    test_dirent_builder();
    test_dirent_bledy();
    test_dispatch_mapping();
    if (g_failures != 0) {
        std::printf("[POSIX-VFS-HOST] FAIL %d/%d\n", g_failures, g_checks);
        return 1;
    }
    std::printf("[POSIX-VFS-HOST] PASS %d asercji\n", g_checks);
    return 0;
}
