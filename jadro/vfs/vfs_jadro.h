#pragma once
#include "vfs.h"
#include <stdint.h>

/*
 * Bursztyn OS - VFS, strona jadra (jedyna warstwa dotykajaca PSF).
 *
 * Podzial: vfs.cpp zna wylacznie tablice deskryptorow i polityke. Ten
 * modul tlumaczy deskryptor na operacje systemu plikow i jest jedynym
 * miejscem, gdzie VFS i PSF sie spotykaja.
 *
 * Bezpieczenstwo:
 *   - kazdy bufor Ring 3 przechodzi przez copy_from_user/copy_to_user
 *     (SMAP), a nie przez bezposredni dostep,
 *   - uprawnienia PZB sprawdzane sa PRZED otwarciem, nie po,
 *   - kazda operacja sprawdza, ze PID wywolujacego jest wlascicielem fd.
 */

/* =========================================================================
 * INICJALIZACJA I SPRZATANIE
 * ========================================================================= */

/* Zeruje tablice. Wywolywane raz, przy starcie podsystemu plikow. */
void vfs_jadro_inicjalizuj();

/*
 * Zwolnienie wszystkich deskryptorow procesu przy recyklingu PID.
 * Wywolywane z posix_zwolnij_proces(). Zwraca liczbe zamknietych fd
 * (do diagnostyki wyciekow).
 */
uint32_t vfs_jadro_zwolnij_proces(int pid);

/* Przygotowuje deskryptory 0/1/2 dla nowo uruchomionego procesu. */
void vfs_jadro_przygotuj_proces(int pid);

/* =========================================================================
 * OPERACJE (kontrakt ABI SysV: 0/dodatnie = sukces, -errno = blad)
 * ========================================================================= */

/*
 * open(2) z uprawnieniami procesu. Tworzy plik przy O_CREAT, obcina przy
 * O_TRUNC, sprawdza PZB dla sciezek chronionych.
 */
int64_t vfs_jadro_open(uint32_t pid, uint64_t sciezka_uzytkownika,
                       uint64_t flagi, uint64_t tryb);

/* openat(2): obecnie tylko AT_FDCWD i sciezki absolutne. */
int64_t vfs_jadro_openat(uint32_t pid, int32_t dirfd,
                         uint64_t sciezka_uzytkownika,
                         uint64_t flagi, uint64_t tryb);

/* close(2). */
int64_t vfs_jadro_close(uint32_t pid, int32_t fd);

/* Rezerwacja fd dla POSIX socketu; stan TCP pozostaje poza VFS. */
int64_t vfs_jadro_zarezerwuj_gniazdo(uint32_t pid);
bool vfs_jadro_czy_gniazdo(uint32_t pid, int32_t fd);
int64_t vfs_jadro_zwolnij_gniazdo(uint32_t pid, int32_t fd);

/*
 * read(2). Dla terminala (fd 0) zwraca -ENOSYS, bo wejscie interaktywne
 * idzie przez BWS 4; pisanie "udawanego" wejscia psuloby aplikacje.
 */
int64_t vfs_jadro_read(uint32_t pid, int32_t fd, uint64_t bufor, uint64_t dlugosc);

/*
 * write(2). fd 1/2 idzie na terminal jadra (jak BWS 1), plik przez PSF.
 * Bufor Ring 3 jest kopiowany do bufora jadra przed zapisem - PSF nie moze
 * pisac bezposrednio do pamieci uzytkownika (SMAP).
 */
int64_t vfs_jadro_write(uint32_t pid, int32_t fd, uint64_t bufor, uint64_t dlugosc);

/* lseek(2). */
int64_t vfs_jadro_lseek(uint32_t pid, int32_t fd, int64_t przesuniecie, int32_t skad);

/* fstat(2): wypelnia VfsStat w pamieci Ring 3. */
int64_t vfs_jadro_fstat(uint32_t pid, int32_t fd, uint64_t bufor_stat);

/* stat(2) po sciezce: to samo co fstat, ale bez otwierania fd. */
int64_t vfs_jadro_stat(uint32_t pid, uint64_t sciezka_uzytkownika, uint64_t bufor_stat);

/* newfstatat(2): obecnie tylko AT_FDCWD i sciezki absolutne. */
int64_t vfs_jadro_newfstatat(uint32_t pid, int32_t dirfd,
                             uint64_t sciezka_uzytkownika,
                             uint64_t bufor_stat, uint32_t flagi);

/* PSF nie ma symlinkow; istniejące obiekty zwracaja EINVAL, brak ENOENT. */
int64_t vfs_jadro_readlink(uint32_t pid, uint64_t sciezka_uzytkownika,
                           uint64_t bufor, uint64_t dlugosc);

/* getdents64(2) na deskryptorze VFS_KATALOG. */
int64_t vfs_jadro_getdents64(uint32_t pid, int32_t fd,
                             uint64_t bufor, uint64_t dlugosc);

/* Eksport tablicy dla diagnostyki (tylko odczyt). */
const VfsDeskryptor* vfs_jadro_podejrzyj(uint32_t pid, int32_t fd);

/*
 * Czy sciezka istnieje (plik albo katalog). Nie kopiuje niczego do Ring 3,
 * wiec nadaje sie dla access(2), ktore pyta o istnienie bez bufora stat.
 */
bool vfs_jadro_istnieje(uint32_t pid, uint64_t sciezka_uzytkownika);

/* =========================================================================
 * STALE ABI
 * ========================================================================= */

enum VfsSeek : int32_t {
    VFS_SEEK_SET = 0,
    VFS_SEEK_CUR = 1,
    VFS_SEEK_END = 2
};

/* Maksymalna dlugosc jednego read/write - zapobiega blokowaniu jadra. */
inline constexpr uint64_t VFS_MAKS_IO = 64ULL * 1024ULL;
