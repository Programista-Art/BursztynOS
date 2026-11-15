#pragma once
#include <stdint.h>
#include <stddef.h>

/*
 * Bursztyn OS - warstwa VFS: tablica deskryptorow plikow per proces.
 *
 * Powod istnienia: bramka BWS nie ma deskryptorow - operacje plikowe
 * przyjmuja pelna sciezke przy kazdym wywolaniu. Kazda libc POSIX (musl,
 * glibc, CPython) zaklada odwrotny model: `open()` zwraca maly licznik,
 * a `read()`/`write()`/`lseek()`/`fstat()` dzialaja na tym liczniku bez
 * powtarzania sciezki.
 *
 * Podzial odpowiedzialnosci jest celowo ostry:
 *
 *   vfs.cpp       - czysta logika: tablica, przydzial fd, walidacja flag,
 *                   uprawnienia odczytu/zapisu. Kompiluje sie na hoscie
 *                   pod ASan/UBSan bez jadra i bez PSF.
 *   vfs_jadro.cpp - jedyne miejsce, ktore dotyka PSF. Tlumaczy deskryptor
 *                   na wywolania systemu plikow.
 *
 * Bezpieczenstwo:
 *   - fd jest indeksem, nie wskaznikiem: uzytkownik nie moze podac dowolnego
 *     adresu deskryptora,
 *   - kazda operacja sprawdza PID wlasciciela, wiec proces nie siegnie do
 *     cudzego deskryptora nawet przy zgadnietym numerze,
 *   - fd 0/1/2 sa zarezerwowane na terminal i nie da sie ich "otworzyc",
 *   - PID-y sa recyklingowane, wiec zwolnienie procesu MUSI wyczyscic cala
 *     tablice - inaczej nowy proces odziedziczylby cudze pliki.
 */

/* =========================================================================
 * LIMITY
 * ========================================================================= */

/*
 * Liczba slotow procesow, dla ktorych istnieje tablica deskryptorow.
 *
 * Wartosc jest powtorzona celowo: vfs.cpp musi kompilowac sie na hoscie bez
 * pzb.h (ktory ciagnie za soba ABI schedulera). Rozjazd z PZB_MAKS_PROCESOW
 * wykrywa static_assert w vfs_jadro.cpp, ktory widzi oba naglowki.
 */
inline constexpr uint32_t VFS_MAKS_PROCESOW = 16U;

/*
 * 64 deskryptory to kompromis: wystarcza musl, busyboxowi i CPythonowi
 * w trybie podstawowym.
 *
 * Tablica jest tablica WSKAZNIKOW, nie struktur. Deskryptor ma ~280 B
 * (glownie sciezka), wiec statyczna tablica 16 x 64 struktur zajmowalaby
 * 280 KiB BSS jadra - tyle nie ma, bo obraz jadra konczy sie dokladnie
 * na limicie linkera. Wskazniki zajmuja 8 KiB, a deskryptor jest
 * alokowany dopiero przy open() i zwalniany przy close().
 */
inline constexpr uint32_t VFS_MAKS_DESKRYPTOROW = 64U;

/*
 * Sciezka przechowywana w deskryptorze. Krotsza niz BWS (512 B), bo dluzej
 * przechowywana sciezka zajmowalaby wiecej niz sam deskryptor. Sciezki
 * dluzsze sa odrzucane przy open(), a nie po cichu obcinane - obciecie
 * zmieniloby plik, na ktory wskazuje deskryptor.
 */
inline constexpr uint32_t VFS_MAKS_SCIEZKA = 256U;

/* Terminal: trzy standardowe deskryptory, zawsze obecne w nowym procesie. */
inline constexpr int VFS_FD_STDIN = 0;
inline constexpr int VFS_FD_STDOUT = 1;
inline constexpr int VFS_FD_STDERR = 2;

/* =========================================================================
 * ODMIANY DESKRYPTORA
 * ========================================================================= */

enum VfsRodzaj : uint32_t {
    VFS_WOLNY = 0,      /* slot nieuzywany */
    VFS_TERMINAL,       /* fd 0/1/2: read -> wejscie, write -> log jadra */
    VFS_PLIK,           /* zwykly plik PSF */
    VFS_KATALOG,        /* katalog: read() zwraca EISDIR, opendir osobno */
    VFS_GNIAZDO         /* POSIX socket; stan transportu jest poza VFS */
};

/* =========================================================================
 * FLAGI OTWARCIA (wartosci linuxowe; czesc ABI, nie nasz wybor)
 * ========================================================================= */

enum VfsFlagiOtwarcia : uint32_t {
    VFS_O_RDONLY = 0x0000U,
    VFS_O_WRONLY = 0x0001U,
    VFS_O_RDWR   = 0x0002U,
    VFS_O_ACCMODE = 0x0003U,
    VFS_O_CREAT  = 0x0040U,
    VFS_O_EXCL   = 0x0080U,
    VFS_O_TRUNC  = 0x0200U,
    VFS_O_APPEND = 0x0400U,
    VFS_O_DIRECTORY = 0x10000U
};

/* =========================================================================
 * DESKRYPTOR
 * ========================================================================= */

struct VfsDeskryptor {
    VfsRodzaj rodzaj;
    /* Wlasciciel. 0 = slot pusty. Sprawdzany przy KAZDEJ operacji. */
    uint32_t pid;
    /* Flagi z open(); decyduja o prawie do odczytu i zapisu. */
    uint32_t flagi;
    /* Biezaca pozycja w pliku. Dla terminala nieuzywana. */
    uint64_t przesuniecie;
    /* Sciezka w PSF. Dla terminala pusty string. */
    char sciezka[VFS_MAKS_SCIEZKA];
};

/* =========================================================================
 * WYNIKI OPERACJI
 * ========================================================================= */

/*
 * vfs.cpp nie zna errno z posix_abi.h (chce byc testowalny bez niego),
 * wiec zwraca wlasne kody, a vfs_jadro tlumaczy je na -errno. To jedyne
 * miejsce tlumaczenia - dzieki temu zgodnosc z Linuksem jest sprawdzana
 * w jednym punkcie.
 */
enum VfsWynik : int32_t {
    VFS_OK = 0,
    VFS_EBADF = 1,
    VFS_EMFILE = 2,
    VFS_EINVAL = 3,
    VFS_EACCES = 4,
    VFS_ENAMETOOLONG = 5,
    VFS_EISDIR = 6,
    VFS_ENOTDIR = 7,
    VFS_ENOENT = 8,
    VFS_EEXIST = 9,
    VFS_ENOSPC = 10,
    VFS_EIO = 11,
    VFS_EROFS = 12,
    VFS_EWRONLY = 13
};

const char* vfs_wynik_opis(VfsWynik wynik);

/* =========================================================================
 * CZYSTA LOGIKA (host-testowalna)
 * ========================================================================= */

/*
 * Alokator jest wstrzykiwany: jadro podaje kmalloc/kfree, test hosta
 * malloc/free. Bez tego vfs.cpp musialby linkowac sie z heapem jadra,
 * co wykluczyloby test hosta.
 */
using VfsAlokator = void* (*)(size_t);
using VfsZwolnij = void (*)(void*);
void vfs_ustaw_alokator(VfsAlokator alokuj, VfsZwolnij zwolnij);

/* Zeruje cala tablice. Wywolywane raz przy starcie jadra. */
void vfs_tabela_inicjalizuj();

/*
 * Wyczyszczenie calej tablicy jednego PID. Musi byc wywolane przy kazdym
 * recyklingu PID - inaczej nowy proces widzi deskryptory poprzednika.
 * Zwraca liczbe zamknietych deskryptorow (do diagnostyki wyciekow).
 */
uint32_t vfs_tabela_zwolnij_proces(uint32_t pid);

/*
 * Przygotowuje standardowe deskryptory (0/1/2) dla nowego procesu.
 * Zwraca VFS_OK albo blad. Idempotentne.
 */
VfsWynik vfs_przygotuj_standardowe(uint32_t pid);

/*
 * Rezerwuje deskryptor dla podanej sciezki i flag. Sprawdza, ze sciezka
 * jest poprawna i miesci sie w buforze. Zwraca numer fd (>= 0) albo
 * -1 z kodem bledu w `blad`.
 */
int32_t vfs_zarezerwuj(uint32_t pid, VfsRodzaj rodzaj, const char* sciezka,
                       size_t dlugosc_sciezki, uint32_t flagi,
                       VfsWynik* blad);

/* Zwraca deskryptor albo nullptr. Sprawdza PID wlasciciela. */
VfsDeskryptor* vfs_pobierz(uint32_t pid, int32_t fd);

/* Zamyka deskryptor. Zwraca VFS_EBADF dla zlego fd lub cudzego wlasciciela. */
VfsWynik vfs_zamknij(uint32_t pid, int32_t fd);

/* Walidacja zestawu flag open(). Zwraca VFS_OK albo konkretny blad. */
VfsWynik vfs_waliduj_flagi(uint32_t flagi);

/* Czy deskryptor pozwala na odczyt (O_RDONLY albo O_RDWR). */
bool vfs_moze_odczytac(const VfsDeskryptor* d);

/* Czy deskryptor pozwala na zapis (O_WRONLY albo O_RDWR). */
bool vfs_moze_zapisac(const VfsDeskryptor* d);

/* Liczba zajetych deskryptorow procesu. Do diagnostyki i testow. */
uint32_t vfs_liczba_otwartych(uint32_t pid);

/*
 * Czy deskryptor jest terminalem (0/1/2). Terminale nie maja alokowanego
 * VfsDeskryptor, wiec vfs_pobierz() zwraca dla nich nullptr - te funkcje
 * daja dostep do ich stanu bez dereferencji.
 */
bool vfs_to_terminal(uint32_t pid, int32_t fd);
uint32_t vfs_flagi_terminala(uint32_t pid, int32_t fd);

/* =========================================================================
 * STRUKTURA STAT (ABI Linux x86-64, 144 B)
 * ========================================================================= */

/*
 * Uklad jest dokladnie linuxowy, bo kazdy naglowek <sys/stat.h> w obcym
 * libc zaklada te offsety. Zla kolejnosc pol nie daje bledu kompilacji -
 * daje smieci w aplikacji.
 */
struct VfsTimespec {
    int64_t tv_sec;
    int64_t tv_nsec;
};

struct VfsStat {
    uint64_t st_dev;
    uint64_t st_ino;
    uint64_t st_nlink;
    uint32_t st_mode;
    uint32_t st_uid;
    uint32_t st_gid;
    int32_t  __pad0;
    uint64_t st_rdev;
    int64_t  st_size;
    int64_t  st_blksize;
    int64_t  st_blocks;
    VfsTimespec st_atim;
    VfsTimespec st_mtim;
    VfsTimespec st_ctim;
    int64_t  __unused[3];
};
static_assert(sizeof(VfsStat) == 144, "VfsStat musi miec uklad Linux x86-64");

/* Tryby plikow (S_IFMT i czesc uprawnien). */
inline constexpr uint32_t VFS_S_IFMT  = 0xF000U;
inline constexpr uint32_t VFS_S_IFDIR = 0x4000U;
inline constexpr uint32_t VFS_S_IFREG = 0x8000U;
inline constexpr uint32_t VFS_S_IRUSR = 0x0100U;
inline constexpr uint32_t VFS_S_IWUSR = 0x0080U;
inline constexpr uint32_t VFS_S_IXUSR = 0x0040U;
inline constexpr uint32_t VFS_S_IRGRP = 0x0020U;
inline constexpr uint32_t VFS_S_IROTH = 0x0004U;

/* Tryb dla zwyklego pliku 0644 i katalogu 0755 - najczestsze w praktyce. */
inline constexpr uint32_t VFS_TRYB_PLIK    = VFS_S_IFREG | 0x1A4U;
inline constexpr uint32_t VFS_TRYB_KATALOG = VFS_S_IFDIR | 0x1EDU;

/*
 * Czysty builder rekordow getdents64. Nie dotyka pamieci Ring 3 ani PSF:
 * vfs_jadro.cpp kopiuje wynik do bufora uzytkownika dopiero po zbudowaniu
 * calych rekordow w pamieci jadra. Dzięki temu format i limity sa testowalne
 * pod ASan/UBSan bez linkowania jadra.
 */
enum VfsDirentTyp : uint8_t {
    VFS_DIRENT_NIEZNANY = 0,
    VFS_DIRENT_FIFO = 1,
    VFS_DIRENT_KATALOG = 4,
    VFS_DIRENT_PLIK = 8
};

/*
 * Buduje jeden linux_dirent64. `nazwa_pojemnosc` jest jawnym limitem
 * odczytu nazwy; funkcja nigdy nie szuka NUL poza tym zakresem.
 */
bool vfs_zbuduj_linux_dirent64(
    void* bufor,
    size_t pojemnosc,
    uint64_t inode,
    uint64_t nastepny_offset,
    uint8_t typ,
    const char* nazwa,
    size_t nazwa_pojemnosc,
    size_t* zapisano);
