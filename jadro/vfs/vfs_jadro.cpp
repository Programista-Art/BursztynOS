/*
 * Bursztyn OS - VFS, strona jadra.
 *
 * Ten plik jest jedynym miejscem, gdzie tablica deskryptorow spotyka sie
 * z PSF, polityka PZB i pamiecia Ring 3. Cala polityka deskryptorow
 * (przydzial, izolacja PID, flagi) jest w vfs.cpp i testowana na hoscie.
 *
 * Zasady, ktorych ten plik nie moze zlamac:
 *
 *   - zaden wskaznik Ring 3 nie jest dereferencjonowany bezposrednio;
 *     kazdy odczyt/zapis idzie przez skopiuj_*_przestrzeni_uzytkownika
 *     (SMAP: bez okna AC dostalibysmy #PF w Ring 0),
 *   - uprawnienia PZB sprawdzamy PRZED mutacja systemu plikow,
 *   - bledy wracaja jako -errno z zakresu -1..-4095 (konwencja ABI SysV),
 *   - nie alokujemy na sciezce syscallu.
 */
#include "vfs_jadro.h"

#include "jadro/posix/posix_abi.h"
#include "jadro/bws/pzb.h"
#include "jadro/fs/psf.h"
#include "jadro/mm/heap.h"

#include <stdint.h>
#include <stddef.h>

extern proces_t tablica_procesow[];

/*
 * Tablica VFS jest indeksowana PID-em, wiec jej liczba wierszy MUSI byc
 * zgodna z limitem procesow PZB. Rozjazd oznaczalby, ze proces dostaje
 * PID, dla ktorego nie ma wiersza - open() zwracalby wtedy EBADF bez
 * powodu. Kompilator lapie to tutaj, a nie w QEMU.
 */
#include "jadro/vfs/vfs.h"
static_assert(VFS_MAKS_PROCESOW == PZB_MAKS_PROCESOW,
              "tablica VFS musi miec tyle wierszy, ile slotow procesow PZB");

/*
 * Wspolne helpery bezpieczenstwa (jadro/bws/bezpieczenstwo.cpp). Kopiuja
 * przez okno SMAP i waliduja zakres strona po stronie.
 */
extern bool skopiuj_z_przestrzeni_uzytkownika(void*, const void*, size_t);
extern bool skopiuj_do_przestrzeni_uzytkownika(void*, const void*, size_t);
extern bool czy_bezpieczny_zakres_uzytkownika_do_odczytu(const void*, size_t);
extern bool czy_bezpieczny_zakres_uzytkownika_do_zapisu(void*, size_t);

/*
 * Polityka PZB jest zaimplementowana w jadro/bws/syscalls.cpp i uzywana
 * przez BWS. Te symbole maja wiazanie wewnetrzne (anonimowa przestrzen
 * nazw) i sa niedostepne dla VFS, dlatego reguly odtwarzamy tutaj z tych
 * samych stalych PZB. To NIE jest duplikacja polityki: sprawdzamy dokladnie
 * te same bity PRAWO_* i ten sam prog PZB_UZYTKOWNIK co BWS.
 */
namespace {

/* Segment-prefix: "/system" pasuje do "/system" i "/system/x", ale nie "/system2". */
bool prefiks_segmentu(const char* sciezka, const char* prefiks) {
    if (!sciezka || !prefiks) return false;
    size_t i = 0;
    while (prefiks[i] != '\0') {
        if (sciezka[i] != prefiks[i]) return false;
        ++i;
    }
    return sciezka[i] == '\0' || sciezka[i] == '/';
}

/* Te same drzewa, ktorych BWS broni przed zapisem z PZB >= UZYTKOWNIK. */
bool drzewo_chronione(const char* sciezka) {
    static const char* const KORZENIE[] = {
        "/system", "/jadro", "/sterowniki", "/programy", "/sloty"
    };
    for (const char* k : KORZENIE) {
        if (prefiks_segmentu(sciezka, k)) return true;
    }
    return false;
}

/* Czy proces ma wszystkie podane prawa (te same bity co BWS). */
bool ma_prawa(const proces_t& p, uint64_t wymagane) {
    return (p.uprawnienia & wymagane) == wymagane;
}

/*
 * Dokladnie ta sama regula co proces_moze_modyfikowac_sciezke w BWS:
 * prawo zapisu ORAZ nie chronione drzewo dla poziomu >= PZB_UZYTKOWNIK.
 */
bool moze_modyfikowac(const proces_t& p, const char* sciezka) {
    if (!ma_prawa(p, PRAWO_PLIKI_ZAPISZ)) return false;
    if (p.poziom_zaufania >= PZB_UZYTKOWNIK && drzewo_chronione(sciezka)) {
        return false;
    }
    return true;
}

}  // namespace

/* Terminal jadra - ten sam, ktorego uzywa BWS 1 i bramka SysV write(). */
extern "C" void wypisz_na_ekranie(const char* tekst);
extern "C" void SerialLog(const char* tekst);

namespace {

/*
 * Tlumaczenie VfsWynik na -errno. Jedno miejsce w calym systemie, wiec
 * zgodnosc z Linuksem jest sprawdzalna w jednym punkcie.
 */
int64_t blad_errno(VfsWynik wynik) {
    switch (wynik) {
        case VFS_OK: return 0;
        case VFS_EBADF: return posix_blad(POSIX_EBADF);
        case VFS_EMFILE: return posix_blad(POSIX_EMFILE);
        case VFS_EINVAL: return posix_blad(POSIX_EINVAL);
        case VFS_EACCES: return posix_blad(POSIX_EACCES);
        case VFS_ENAMETOOLONG: return posix_blad(POSIX_EINVAL);
        case VFS_EISDIR: return posix_blad(POSIX_EISDIR);
        case VFS_ENOTDIR: return posix_blad(POSIX_ENOTDIR);
        case VFS_ENOENT: return posix_blad(POSIX_ENOENT);
        case VFS_EEXIST: return posix_blad(POSIX_EEXIST);
        case VFS_ENOSPC: return posix_blad(POSIX_ENOSPC);
        case VFS_EIO: return posix_blad(POSIX_EIO);
        case VFS_EROFS: return posix_blad(POSIX_EACCES);
        case VFS_EWRONLY: return posix_blad(POSIX_EBADF);
        default: return posix_blad(POSIX_EINVAL);
    }
}

/*
 * Kopiuje sciezke z Ring 3 do bufora jadra i waliduje ja.
 *
 * Wymagamy sciezki absolutnej. Sciezka relatywna wymagalaby pojecia
 * katalogu biezacego, ktorego proces jeszcze nie ma - udawanie, ze "/"
 * jest katalogiem biezacym, zmienialoby znaczenie sciezek aplikacji.
 */
bool pobierz_sciezke(uint64_t wskaznik, char* cel, size_t pojemnosc,
                     size_t* dlugosc, bool* wzgledna = nullptr) {
    if (wzgledna) *wzgledna = false;
    if (!wskaznik || !cel || pojemnosc < 2) return false;

    /*
     * Sciezka moze konczyc sie na granicy strony, wiec pelna kopia moze
     * zwrocic false mimo poprawnej zawartosci. Wtedy czytamy bajt po
     * bajcie, az do terminatora - tak jak robi to kazdy prawdziwy
     * copy_from_user ze stronicowaniem.
     */
    if (!skopiuj_z_przestrzeni_uzytkownika(
            cel, reinterpret_cast<const void*>(wskaznik), pojemnosc)) {
        size_t i = 0;
        for (; i + 1 < pojemnosc; ++i) {
            char c = 0;
            if (!skopiuj_z_przestrzeni_uzytkownika(
                    &c, reinterpret_cast<const void*>(wskaznik + i), 1)) {
                return false;
            }
            cel[i] = c;
            if (c == '\0') break;
        }
        if (i + 1 >= pojemnosc) return false;  /* brak terminatora */
        cel[i + 1] = '\0';
    }

    size_t n = 0;
    while (n + 1 < pojemnosc && cel[n] != '\0') ++n;
    if (n == 0 || n + 1 > pojemnosc || cel[n] != '\0') return false;
    if (cel[0] != '/') {
        if (wzgledna) *wzgledna = true;
        return false;
    }

    /* Odrzucamy "..", "." i podwojne ukosniki: PSF nie robi normalizacji. */
    for (size_t i = 0; i < n; ++i) {
        if (cel[i] == '\\') return false;
        if (cel[i] == '/' && i + 1 < n && cel[i + 1] == '/') return false;
        if (cel[i] == '.' && i + 1 < n && cel[i + 1] == '.') return false;
        if (cel[i] == '.' && (i == 0 || cel[i - 1] == '/') &&
            (i + 1 == n || cel[i + 1] == '/')) {
            return false;
        }
    }

    if (dlugosc) *dlugosc = n;
    return true;
}

/* Rozmiar pliku w PSF albo 0, gdy plik nie istnieje. */
uint64_t rozmiar_w_psf(const char* sciezka) {
    return static_cast<uint64_t>(rozmiar_pliku(sciezka));
}

/* Czy sciezka istnieje jako plik lub katalog. */
bool istnieje_w_psf(const char* sciezka) {
    return rozmiar_pliku(sciezka) != 0 || czy_katalog_istnieje(sciezka);
}

int64_t vfs_jadro_open_sciezka(uint32_t pid, const char* sciezka,
                               size_t dlugosc, uint64_t flagi,
                               uint64_t tryb) {
    if (pid == 0 || pid >= VFS_MAKS_PROCESOW || !sciezka || dlugosc == 0) {
        return posix_blad(POSIX_EBADF);
    }
    const proces_t& proces = tablica_procesow[pid];
    (void)tryb;  /* PSF nie ma trybow POSIX; VFS_STAT raportuje 0644/0755. */

    VfsWynik walidacja = vfs_waliduj_flagi(static_cast<uint32_t>(flagi));
    if (walidacja != VFS_OK) return blad_errno(walidacja);

    const uint32_t f = static_cast<uint32_t>(flagi);
    const bool chce_zapis = (f & VFS_O_ACCMODE) != VFS_O_RDONLY;
    const bool tworz = (f & VFS_O_CREAT) != 0;

    if (!ma_prawa(proces, PRAWO_PLIKI_CZYTAJ)) {
        return posix_blad(POSIX_EACCES);
    }

    if (chce_zapis || tworz) {
        if (!ma_prawa(proces, PRAWO_PLIKI_ZAPISZ)) {
            return posix_blad(POSIX_EACCES);
        }
        if (!moze_modyfikowac(proces, sciezka)) {
            return posix_blad(POSIX_EACCES);
        }
    }

    const bool istnieje = istnieje_w_psf(sciezka);
    if ((f & VFS_O_EXCL) != 0 && istnieje) {
        return posix_blad(POSIX_EEXIST);
    }

    if (!istnieje) {
        if (!tworz) return posix_blad(POSIX_ENOENT);
        if (czy_katalog_istnieje(sciezka)) {
            return posix_blad(POSIX_EISDIR);
        }
        if (!utworz_plik(sciezka)) return posix_blad(POSIX_EIO);
    } else if (czy_katalog_istnieje(sciezka)) {
        if (chce_zapis) return posix_blad(POSIX_EISDIR);
        VfsWynik blad = VFS_OK;
        const int32_t fd = vfs_zarezerwuj(
            pid, VFS_KATALOG, sciezka, dlugosc, VFS_O_RDONLY, &blad);
        return fd < 0 ? blad_errno(blad) : fd;
    }

    if ((f & VFS_O_TRUNC) != 0) {
        if (!zapisz_do_pliku(sciezka, "", 0)) return posix_blad(POSIX_EIO);
    }

    VfsWynik blad = VFS_OK;
    const int32_t fd = vfs_zarezerwuj(
        pid, VFS_PLIK, sciezka, dlugosc, f, &blad);
    if (fd < 0) return blad_errno(blad);

    if ((f & VFS_O_APPEND) != 0) {
        VfsDeskryptor* d = vfs_pobierz(pid, fd);
        if (d) d->przesuniecie = rozmiar_w_psf(sciezka);
    }
    return fd;
}

}  // namespace

/* =========================================================================
 * INICJALIZACJA I SPRZATANIE
 * ========================================================================= */

void vfs_jadro_inicjalizuj() {
    /*
     * Alokator deskryptorow: kmalloc/kfree jadra. Podanie go tutaj, a nie
     * w vfs.cpp, trzyma warstwe czystej logiki wolna od zaleznosci od
     * alokatora jadra i pozwala testowi hosta uzyc malloc/free.
     */
    vfs_ustaw_alokator(
        [](size_t rozmiar) -> void* { return kmalloc(rozmiar); },
        [](void* p) { kfree(p); });
    vfs_tabela_inicjalizuj();
}

uint32_t vfs_jadro_zwolnij_proces(int pid) {
    if (pid <= 0) return 0;
    return vfs_tabela_zwolnij_proces(static_cast<uint32_t>(pid));
}

void vfs_jadro_przygotuj_proces(int pid) {
    if (pid <= 0) return;
    (void)vfs_przygotuj_standardowe(static_cast<uint32_t>(pid));
}

/* =========================================================================
 * OPEN / CLOSE
 * ========================================================================= */

int64_t vfs_jadro_open(uint32_t pid, uint64_t sciezka_uzytkownika,
                       uint64_t flagi, uint64_t tryb) {
    char sciezka[VFS_MAKS_SCIEZKA]{};
    size_t dlugosc = 0;
    if (!pobierz_sciezke(sciezka_uzytkownika, sciezka, sizeof(sciezka), &dlugosc)) {
        return posix_blad(POSIX_EFAULT);
    }
    return vfs_jadro_open_sciezka(pid, sciezka, dlugosc, flagi, tryb);
}

int64_t vfs_jadro_openat(uint32_t pid, int32_t dirfd,
                         uint64_t sciezka_uzytkownika,
                         uint64_t flagi, uint64_t tryb) {
    if (dirfd != POSIX_AT_FDCWD) return posix_blad(POSIX_EBADF);
    char sciezka[VFS_MAKS_SCIEZKA]{};
    size_t dlugosc = 0;
    bool wzgledna = false;
    if (!pobierz_sciezke(sciezka_uzytkownika, sciezka, sizeof(sciezka),
                         &dlugosc, &wzgledna)) {
        return wzgledna ? posix_blad(POSIX_ENOTSUP)
                        : posix_blad(POSIX_EFAULT);
    }
    return vfs_jadro_open_sciezka(pid, sciezka, dlugosc, flagi, tryb);
}

int64_t vfs_jadro_close(uint32_t pid, int32_t fd) {
    const VfsWynik wynik = vfs_zamknij(pid, fd);
    return wynik == VFS_OK ? 0 : blad_errno(wynik);
}

int64_t vfs_jadro_zarezerwuj_gniazdo(uint32_t pid) {
    VfsWynik blad = VFS_OK;
    const int32_t fd = vfs_zarezerwuj(
        pid, VFS_GNIAZDO, nullptr, 0, VFS_O_RDWR, &blad);
    return fd >= 0 ? fd : blad_errno(blad);
}

bool vfs_jadro_czy_gniazdo(uint32_t pid, int32_t fd) {
    const VfsDeskryptor* d = vfs_pobierz(pid, fd);
    return d != nullptr && d->rodzaj == VFS_GNIAZDO;
}

int64_t vfs_jadro_zwolnij_gniazdo(uint32_t pid, int32_t fd) {
    if (!vfs_jadro_czy_gniazdo(pid, fd)) return posix_blad(POSIX_EBADF);
    return vfs_zamknij(pid, fd) == VFS_OK ? 0 : posix_blad(POSIX_EBADF);
}

/* =========================================================================
 * READ
 * ========================================================================= */

int64_t vfs_jadro_read(uint32_t pid, int32_t fd,
                       uint64_t bufor, uint64_t dlugosc) {
    VfsDeskryptor* d = vfs_pobierz(pid, fd);
    /*
     * Terminal nie ma alokowanego deskryptora, wiec vfs_pobierz() zwraca
     * dla niego nullptr. Rozpoznajemy go osobno - inaczej read(0) zwracalby
     * EBADF zamiast uczciwego -ENOSYS.
     */
    const bool terminal = vfs_to_terminal(pid, fd);
    if (!d && !terminal) return posix_blad(POSIX_EBADF);
    if (d && d->rodzaj == VFS_GNIAZDO) return posix_blad(POSIX_ENOTSOCK);

    if (terminal) {
        const uint32_t tryb = vfs_flagi_terminala(pid, fd) & VFS_O_ACCMODE;
        if (tryb != VFS_O_RDONLY && tryb != VFS_O_RDWR) {
            return posix_blad(POSIX_EBADF);
        }
    } else if (!vfs_moze_odczytac(d)) {
        return posix_blad(POSIX_EBADF);
    }

    if (dlugosc == 0) return 0;
    if (dlugosc > VFS_MAKS_IO) return posix_blad(POSIX_EINVAL);
    if (!bufor) return posix_blad(POSIX_EFAULT);
    if (!czy_bezpieczny_zakres_uzytkownika_do_zapisu(
            reinterpret_cast<void*>(bufor), static_cast<size_t>(dlugosc))) {
        return posix_blad(POSIX_EFAULT);
    }

    if (terminal) {
        /*
         * Wejscie interaktywne idzie przez BWS 4 (pobierz_znak_klawiatury).
         * Czytanie go tutaj wymagaloby blokowania procesu w syscallu,
         * ktorego scheduler jeszcze nie ma. -ENOSYS jest uczciwe: aplikacja
         * dowie sie, ze tej sciezki nie ma, zamiast dostac 0 (EOF).
         */
        return posix_blad(POSIX_ENOSYS);
    }

    if (d->rodzaj == VFS_KATALOG) return posix_blad(POSIX_EISDIR);

    const uint64_t rozmiar = rozmiar_w_psf(d->sciezka);
    if (d->przesuniecie >= rozmiar) return 0;  /* EOF */

    uint64_t do_odczytu = rozmiar - d->przesuniecie;
    if (do_odczytu > dlugosc) do_odczytu = dlugosc;

    /*
     * Odczyt do bufora jadra, potem jedna kontrolowana kopia do Ring 3.
     * PSF nie moze pisac bezposrednio do pamieci uzytkownika - przy SMAP
     * dostalibysmy #PF w Ring 0 (dokladnie ten blad naprawiono w BWS 59).
     */
    static char posredni[VFS_MAKS_IO];
    uint32_t odczytano = 0;
    if (!czytaj_fragment_pliku(d->sciezka, d->przesuniecie, posredni,
                               static_cast<uint32_t>(do_odczytu), &odczytano)) {
        return posix_blad(POSIX_EIO);
    }
    if (odczytano == 0) return 0;

    if (!skopiuj_do_przestrzeni_uzytkownika(
            reinterpret_cast<void*>(bufor), posredni, odczytano)) {
        return posix_blad(POSIX_EFAULT);
    }

    d->przesuniecie += odczytano;
    return static_cast<int64_t>(odczytano);
}

/* =========================================================================
 * WRITE
 * ========================================================================= */

int64_t vfs_jadro_write(uint32_t pid, int32_t fd,
                        uint64_t bufor, uint64_t dlugosc) {
    VfsDeskryptor* d = vfs_pobierz(pid, fd);
    const bool terminal = vfs_to_terminal(pid, fd);
    if (!d && !terminal) return posix_blad(POSIX_EBADF);
    if (d && d->rodzaj == VFS_GNIAZDO) return posix_blad(POSIX_ENOTSOCK);

    if (terminal) {
        const uint32_t tryb = vfs_flagi_terminala(pid, fd) & VFS_O_ACCMODE;
        if (tryb != VFS_O_WRONLY && tryb != VFS_O_RDWR) {
            return posix_blad(POSIX_EBADF);
        }
    } else if (!vfs_moze_zapisac(d)) {
        return posix_blad(POSIX_EBADF);
    }

    if (dlugosc == 0) return 0;
    if (dlugosc > VFS_MAKS_IO) return posix_blad(POSIX_EINVAL);
    if (!bufor) return posix_blad(POSIX_EFAULT);
    if (!czy_bezpieczny_zakres_uzytkownika_do_odczytu(
            reinterpret_cast<const void*>(bufor), static_cast<size_t>(dlugosc))) {
        return posix_blad(POSIX_EFAULT);
    }

    /*
     * Terminal: ten sam mechanizm co BWS 1 i SysV write(1). Kopiujemy
     * kawalkami, bo wypisz_na_ekranie oczekuje stringa zakonczonego NUL
     * i nie przyjmuje dlugosci.
     */
    if (terminal) {
        char bufor_jadra[256];
        uint64_t zapisane = 0;
        const uint64_t porcja_max = sizeof(bufor_jadra) - 1;
        while (zapisane < dlugosc) {
            uint64_t porcja = dlugosc - zapisane;
            if (porcja > porcja_max) porcja = porcja_max;
            if (!skopiuj_z_przestrzeni_uzytkownika(bufor_jadra,
                    reinterpret_cast<const void*>(bufor + zapisane),
                    static_cast<size_t>(porcja))) {
                return posix_blad(POSIX_EFAULT);
            }
            bufor_jadra[porcja] = '\0';
            SerialLog(bufor_jadra);
            wypisz_na_ekranie(bufor_jadra);
            zapisane += porcja;
        }
        return static_cast<int64_t>(zapisane);
    }

    if (d->rodzaj == VFS_KATALOG) return posix_blad(POSIX_EISDIR);

    /* Plik: kopia do bufora jadra, potem zapis fragmentu przez PSF. */
    static char posredni[VFS_MAKS_IO];
    if (!skopiuj_z_przestrzeni_uzytkownika(
            posredni, reinterpret_cast<const void*>(bufor),
            static_cast<size_t>(dlugosc))) {
        return posix_blad(POSIX_EFAULT);
    }

    if (!zapisz_fragment_pliku(d->sciezka, d->przesuniecie, posredni,
                               static_cast<uint32_t>(dlugosc))) {
        return posix_blad(POSIX_EIO);
    }

    d->przesuniecie += dlugosc;
    return static_cast<int64_t>(dlugosc);
}

/* =========================================================================
 * LSEEK
 * ========================================================================= */

int64_t vfs_jadro_lseek(uint32_t pid, int32_t fd,
                        int64_t przesuniecie, int32_t skad) {
    VfsDeskryptor* d = vfs_pobierz(pid, fd);
    const bool terminal = vfs_to_terminal(pid, fd);
    if (!d && !terminal) return posix_blad(POSIX_EBADF);
    if (terminal) return posix_blad(POSIX_ESPIPE);
    if (d->rodzaj == VFS_KATALOG) return posix_blad(POSIX_EISDIR);

    const uint64_t rozmiar = rozmiar_w_psf(d->sciezka);

    int64_t baza = 0;
    switch (skad) {
        case VFS_SEEK_SET: baza = 0; break;
        case VFS_SEEK_CUR: baza = static_cast<int64_t>(d->przesuniecie); break;
        case VFS_SEEK_END: baza = static_cast<int64_t>(rozmiar); break;
        default: return posix_blad(POSIX_EINVAL);
    }

    /* Ujemny wynik jest bledem - przesuniecie przed poczatek pliku. */
    if (przesuniecie < 0 && baza < -przesuniecie) {
        return posix_blad(POSIX_EINVAL);
    }
    const int64_t nowe = baza + przesuniecie;
    if (nowe < 0) return posix_blad(POSIX_EINVAL);

    /*
     * Zezwalamy na pozycje za koncem pliku (POSIX): nastepny zapis
     * wypelnilby luke zerami. PSF nie ma tej operacji, wiec zapis poza
     * koniec zwroci EIO - lepiej niz ciche nadpisanie zlego offsetu.
     */
    d->przesuniecie = static_cast<uint64_t>(nowe);
    return nowe;
}

/* =========================================================================
 * STAT
 * ========================================================================= */

namespace {

/* Wypelnia VfsStat na podstawie metadanych PSF. */
bool zbuduj_stat(const char* sciezka, VfsStat* st) {
    if (!st) return false;

    const bool katalog = czy_katalog_istnieje(sciezka);
    const uint32_t rozmiar = rozmiar_pliku(sciezka);
    if (!katalog && rozmiar == 0) return false;  /* nie istnieje */

    *st = {};
    st->st_dev = 1;          /* jedno urzadzenie: PSF na nosniku blokowym */
    /*
     * inode: PSF nie eksportuje numeru wezla w API publicznym. Uzywamy
     * stabilnego skrotu sciezki, zeby aplikacja mogla odroznic dwa pliki;
     * nie jest to prawdziwy numer inode i nie udajemy, ze jest - pole
     * sluzy tylko do porownan w obrebie procesu.
     */
    uint64_t hash = 14695981039346656037ULL;
    for (size_t i = 0; sciezka[i]; ++i) {
        hash = (hash ^ static_cast<uint8_t>(sciezka[i])) * 1099511628211ULL;
    }
    st->st_ino = hash;
    st->st_nlink = 1;
    st->st_mode = katalog ? VFS_TRYB_KATALOG : VFS_TRYB_PLIK;
    st->st_uid = 0;
    st->st_gid = 0;
    st->st_rdev = 0;
    st->st_size = katalog ? 0 : static_cast<int64_t>(rozmiar);
    st->st_blksize = 4096;
    st->st_blocks = katalog ? 0
                            : static_cast<int64_t>((rozmiar + 511) / 512);
    return true;
}

int64_t wypisz_stat(uint64_t bufor_uzytkownika, const VfsStat& st) {
    if (!bufor_uzytkownika) return posix_blad(POSIX_EFAULT);
    if (!czy_bezpieczny_zakres_uzytkownika_do_zapisu(
            reinterpret_cast<void*>(bufor_uzytkownika), sizeof(st))) {
        return posix_blad(POSIX_EFAULT);
    }
    if (!skopiuj_do_przestrzeni_uzytkownika(
            reinterpret_cast<void*>(bufor_uzytkownika), &st, sizeof(st))) {
        return posix_blad(POSIX_EFAULT);
    }
    return 0;
}

int64_t vfs_jadro_stat_sciezka(uint32_t pid, const char* sciezka,
                               uint64_t bufor_stat) {
    (void)pid;
    if (!sciezka) return posix_blad(POSIX_EFAULT);
    VfsStat st{};
    if (!zbuduj_stat(sciezka, &st)) return posix_blad(POSIX_ENOENT);
    return wypisz_stat(bufor_stat, st);
}

}  // namespace

int64_t vfs_jadro_fstat(uint32_t pid, int32_t fd, uint64_t bufor_stat) {
    const VfsDeskryptor* d = vfs_pobierz(pid, fd);
    const bool terminal = vfs_to_terminal(pid, fd);
    if (!d && !terminal) return posix_blad(POSIX_EBADF);

    VfsStat st{};
    if (terminal) {
        /*
         * Terminal jest urzadzeniem znakowym, nie plikiem. Raportujemy
         * to spojnie, zeby `isatty()` w libc mial poprawna odpowiedz.
         */
        st.st_dev = 1;
        st.st_ino = static_cast<uint64_t>(fd) + 1000;
        st.st_nlink = 1;
        st.st_mode = 0x2000U | 0x1B6U;  /* S_IFCHR | 0666 */
        st.st_blksize = 4096;
        return wypisz_stat(bufor_stat, st);
    }

    if (!zbuduj_stat(d->sciezka, &st)) return posix_blad(POSIX_ENOENT);
    return wypisz_stat(bufor_stat, st);
}

int64_t vfs_jadro_stat(uint32_t pid, uint64_t sciezka_uzytkownika,
                       uint64_t bufor_stat) {
    char sciezka[VFS_MAKS_SCIEZKA]{};
    size_t dlugosc = 0;
    if (!pobierz_sciezke(sciezka_uzytkownika, sciezka, sizeof(sciezka), &dlugosc)) {
        return posix_blad(POSIX_EFAULT);
    }
    return vfs_jadro_stat_sciezka(pid, sciezka, bufor_stat);
}

int64_t vfs_jadro_newfstatat(uint32_t pid, int32_t dirfd,
                             uint64_t sciezka_uzytkownika,
                             uint64_t bufor_stat, uint32_t flagi) {
    if (dirfd != POSIX_AT_FDCWD) return posix_blad(POSIX_EBADF);
    if (flagi & ~(POSIX_AT_SYMLINK_NOFOLLOW)) {
        return posix_blad(POSIX_EINVAL);
    }
    char sciezka[VFS_MAKS_SCIEZKA]{};
    size_t dlugosc = 0;
    bool wzgledna = false;
    if (!pobierz_sciezke(sciezka_uzytkownika, sciezka, sizeof(sciezka),
                         &dlugosc, &wzgledna)) {
        return wzgledna ? posix_blad(POSIX_ENOTSUP)
                        : posix_blad(POSIX_EFAULT);
    }
    return vfs_jadro_stat_sciezka(pid, sciezka, bufor_stat);
}

int64_t vfs_jadro_readlink(uint32_t pid, uint64_t sciezka_uzytkownika,
                           uint64_t bufor, uint64_t dlugosc) {
    (void)pid;
    char sciezka[VFS_MAKS_SCIEZKA]{};
    size_t dlugosc_sciezki = 0;
    if (!pobierz_sciezke(sciezka_uzytkownika, sciezka, sizeof(sciezka),
                         &dlugosc_sciezki)) {
        return posix_blad(POSIX_EFAULT);
    }
    /*
     * PSF ma tylko TYP_PLIK i TYP_KATALOG, bez dowiazan symbolicznych.
     * Najpierw rozroznij brak sciezki od istniejacego obiektu; nie dotykaj
     * bufora wyjsciowego, bo nie istnieje bezpieczny cel do skopiowania.
     */
    if (!istnieje_w_psf(sciezka)) return posix_blad(POSIX_ENOENT);
    (void)bufor;
    (void)dlugosc;
    return posix_blad(POSIX_EINVAL);
}

int64_t vfs_jadro_getdents64(uint32_t pid, int32_t fd,
                             uint64_t bufor, uint64_t dlugosc) {
    VfsDeskryptor* d = vfs_pobierz(pid, fd);
    if (!d) return posix_blad(POSIX_EBADF);
    if (d->rodzaj != VFS_KATALOG) return posix_blad(POSIX_ENOTDIR);
    if (dlugosc == 0) return 0;
    if (dlugosc > VFS_MAKS_IO) return posix_blad(POSIX_EINVAL);
    if (!bufor || !czy_bezpieczny_zakres_uzytkownika_do_zapisu(
                       reinterpret_cast<void*>(bufor),
                       static_cast<size_t>(dlugosc))) {
        return posix_blad(POSIX_EFAULT);
    }

    static uint8_t posredni[VFS_MAKS_IO];
    size_t zapisano = 0;
    uint64_t indeks = d->przesuniecie;
    while (zapisano < static_cast<size_t>(dlugosc)) {
        PsfKatalogWpis wpis{};
        const PsfKatalogWynik wynik =
            psf_pobierz_wpis_katalogu(d->sciezka, indeks, &wpis);
        if (wynik == PSF_KATALOG_BLAD) return posix_blad(POSIX_EIO);
        if (wynik == PSF_KATALOG_KONIEC) break;
        if (indeks == UINT64_MAX) return posix_blad(POSIX_EOVERFLOW);

        size_t rekord = 0;
        const uint8_t typ = wpis.typ == TYP_KATALOG
                                ? VFS_DIRENT_KATALOG
                                : VFS_DIRENT_PLIK;
        if (!vfs_zbuduj_linux_dirent64(
                posredni + zapisano,
                static_cast<size_t>(dlugosc) - zapisano,
                wpis.inode, indeks + 1U, typ, wpis.nazwa,
                sizeof(wpis.nazwa), &rekord)) {
            if (zapisano == 0) return posix_blad(POSIX_EINVAL);
            break;
        }
        zapisano += rekord;
        ++indeks;
    }

    if (zapisano == 0) {
        d->przesuniecie = indeks;
        return 0;
    }
    if (!skopiuj_do_przestrzeni_uzytkownika(
            reinterpret_cast<void*>(bufor), posredni, zapisano)) {
        return posix_blad(POSIX_EFAULT);
    }
    d->przesuniecie = indeks;
    return static_cast<int64_t>(zapisano);
}

const VfsDeskryptor* vfs_jadro_podejrzyj(uint32_t pid, int32_t fd) {
    return vfs_pobierz(pid, fd);
}

bool vfs_jadro_istnieje(uint32_t pid, uint64_t sciezka_uzytkownika) {
    (void)pid;
    char sciezka[VFS_MAKS_SCIEZKA]{};
    size_t dlugosc = 0;
    if (!pobierz_sciezke(sciezka_uzytkownika, sciezka, sizeof(sciezka), &dlugosc)) {
        return false;
    }
    return istnieje_w_psf(sciezka);
}
