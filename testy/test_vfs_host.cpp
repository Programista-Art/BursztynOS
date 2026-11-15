/*
 * Bursztyn OS - test hosta warstwy VFS (tablica deskryptorow).
 *
 * Zakres: wylacznie czysta logika z jadro/vfs/vfs.cpp. Test nie dotyka PSF
 * ani jadra, wiec kazde naruszenie kontraktu jest tu widoczne bez QEMU.
 *
 * Sprawdzane kategorie:
 *
 *   1. przydzial i zwalnianie deskryptorow,
 *   2. standardowe 0/1/2 i ich tryby,
 *   3. izolacja miedzy procesami (cudzy fd jest niewidoczny),
 *   4. recykling PID nie dziedziczy deskryptorow,
 *   5. limity: EMFILE, ENAMETOOLONG,
 *   6. walidacja flag (sprzeczne kombinacje),
 *   7. prawa odczytu i zapisu wg flag,
 *   8. ABI VfsStat (144 B, uklad pol),
 *   9. odpornosc na zle argumenty (fd ujemne, pid 0, pid poza zakresem).
 */
#include "jadro/vfs/vfs.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

int g_niepowodzenia = 0;
int g_sprawdzenia = 0;

void sprawdz(bool warunek, const char* opis) {
    ++g_sprawdzenia;
    if (!warunek) {
        std::printf("FAIL: %s\n", opis);
        ++g_niepowodzenia;
    }
}

/* =========================================================================
 * 1. PRZYDZIAL I ZWALNIANIE
 * ========================================================================= */

void test_przydzial() {
    vfs_tabela_inicjalizuj();
    sprawdz(vfs_przygotuj_standardowe(1) == VFS_OK, "std fd dla procesu 1");

    VfsWynik blad = VFS_EIO;
    const int32_t fd = vfs_zarezerwuj(1, VFS_PLIK, "/dane/test.txt", 14,
                                      VFS_O_RDONLY, &blad);
    sprawdz(fd == 3, "pierwszy plik dostaje fd 3 (0/1/2 zajete)");
    sprawdz(blad == VFS_OK, "brak bledu przy poprawnym open");

    /* Ten sam plik drugi raz -> kolejny numer, nie ten sam slot. */
    const int32_t fd2 = vfs_zarezerwuj(1, VFS_PLIK, "/dane/test.txt", 14,
                                       VFS_O_RDONLY, &blad);
    sprawdz(fd2 == 4, "drugie otwarcie dostaje fd 4");

    /* Sciezka jest kopiowana, nie zapamietana przez wskaznik. */
    VfsDeskryptor* d = vfs_pobierz(1, fd);
    sprawdz(d != nullptr, "pobranie wlasnego fd");
    if (d) {
        sprawdz(std::strcmp(d->sciezka, "/dane/test.txt") == 0,
                "sciezka skopiowana do deskryptora");
        sprawdz(d->przesuniecie == 0, "nowy deskryptor ma offset 0");
        sprawdz(d->rodzaj == VFS_PLIK, "rodzaj pliku zapisany");
    }

    sprawdz(vfs_liczba_otwartych(1) == 5, "3 terminale + 2 pliki");

    sprawdz(vfs_zamknij(1, fd) == VFS_OK, "zamkniecie fd 3");
    sprawdz(vfs_pobierz(1, fd) == nullptr, "zamkniety fd jest niewidoczny");
    sprawdz(vfs_zamknij(1, fd) == VFS_EBADF, "podwojne zamkniecie -> EBADF");

    /*
     * Po zamknieciu fd 3 nastepne open musi je odzyskac: inaczej dlugotrwaly
     * proces wyczerpalby 64 deskryptory samym otwieraniem i zamykaniem.
     */
    const int32_t fd3 = vfs_zarezerwuj(1, VFS_PLIK, "/dane/b.txt", 11,
                                       VFS_O_RDONLY, &blad);
    sprawdz(fd3 == 3, "zwolniony slot jest uzywany ponownie");
}

void test_socket_fd_izolacja() {
    vfs_tabela_inicjalizuj();
    sprawdz(vfs_przygotuj_standardowe(3) == VFS_OK, "socket: std fd");
    VfsWynik blad = VFS_EIO;
    const int32_t socket_fd = vfs_zarezerwuj(
        3, VFS_GNIAZDO, nullptr, 0, VFS_O_RDWR, &blad);
    sprawdz(socket_fd == 3, "socket: fd nie koliduje z terminalami");
    sprawdz(blad == VFS_OK, "socket: rezerwacja bez sciezki");
    VfsDeskryptor* d = vfs_pobierz(3, socket_fd);
    sprawdz(d != nullptr && d->rodzaj == VFS_GNIAZDO,
            "socket: rekord ma typ gniazda");
    sprawdz(vfs_pobierz(4, socket_fd) == nullptr,
            "socket: fd nie przecieka miedzy PID");
    const int32_t file_fd = vfs_zarezerwuj(
        3, VFS_PLIK, "/socket-sidecar", 15, VFS_O_RDONLY, &blad);
    sprawdz(file_fd == 4, "socket: plik dostaje kolejny wolny fd");
    sprawdz(vfs_zamknij(3, socket_fd) == VFS_OK,
            "socket: zwolnienie wpisu");
    sprawdz(vfs_pobierz(3, socket_fd) == nullptr,
            "socket: zwolniony fd jest niewidoczny");
}

/* =========================================================================
 * 2. STANDARDOWE 0/1/2
 * ========================================================================= */

void test_standardowe() {
    vfs_tabela_inicjalizuj();
    sprawdz(vfs_przygotuj_standardowe(2) == VFS_OK, "std fd dla procesu 2");

    /*
     * Terminale nie maja alokowanego VfsDeskryptor (oszczednosc BSS),
     * wiec ich stan czytamy przez vfs_to_terminal/vfs_flagi_terminala.
     */
    sprawdz(vfs_to_terminal(2, VFS_FD_STDIN), "stdin jest terminalem");
    sprawdz(vfs_to_terminal(2, VFS_FD_STDOUT), "stdout jest terminalem");
    sprawdz(vfs_to_terminal(2, VFS_FD_STDERR), "stderr jest terminalem");
    sprawdz(!vfs_to_terminal(2, 3), "fd 3 nie jest terminalem");

    const uint32_t f_in = vfs_flagi_terminala(2, VFS_FD_STDIN);
    const uint32_t f_out = vfs_flagi_terminala(2, VFS_FD_STDOUT);
    const uint32_t f_err = vfs_flagi_terminala(2, VFS_FD_STDERR);
    sprawdz((f_in & VFS_O_ACCMODE) == VFS_O_RDONLY, "stdin: tylko odczyt");
    sprawdz((f_out & VFS_O_ACCMODE) == VFS_O_WRONLY, "stdout: tylko zapis");
    sprawdz((f_err & VFS_O_ACCMODE) == VFS_O_WRONLY, "stderr: tylko zapis");

    /* vfs_pobierz celowo nie zwraca deskryptora dla terminala. */
    sprawdz(vfs_pobierz(2, VFS_FD_STDIN) == nullptr,
            "terminal nie ma alokowanego deskryptora");

    /* Idempotencja: drugie przygotowanie nie moze zepsuc stanu. */
    sprawdz(vfs_przygotuj_standardowe(2) == VFS_OK, "powtorne przygotowanie std");
    sprawdz(vfs_to_terminal(2, VFS_FD_STDIN),
            "stdin nadal terminalem po powtorzeniu");
    sprawdz((vfs_flagi_terminala(2, VFS_FD_STDIN) & VFS_O_ACCMODE) == VFS_O_RDONLY,
            "stdin nadal tylko do odczytu");

    /*
     * Zamkniecie stdout nie moze oddac numeru 1 plikowi: inaczej zapis
     * "na stdout" zaczalby pisac do przypadkowego pliku uzytkownika.
     */
    sprawdz(vfs_zamknij(2, VFS_FD_STDOUT) == VFS_OK, "zamkniecie stdout");
    VfsWynik blad = VFS_EIO;
    const int32_t fd = vfs_zarezerwuj(2, VFS_PLIK, "/x", 2, VFS_O_RDONLY, &blad);
    sprawdz(fd == 3, "po zamknieciu stdout plik dostaje fd 3, nie 1");
    sprawdz(vfs_to_terminal(2, VFS_FD_STDOUT),
            "slot stdout pozostaje terminalem");
    sprawdz(vfs_flagi_terminala(2, VFS_FD_STDOUT) == 0,
            "zamkniety stdout nie pozwala pisac");
}

/* =========================================================================
 * 3. IZOLACJA MIEDZY PROCESAMI
 * ========================================================================= */

void test_izolacja() {
    vfs_tabela_inicjalizuj();
    sprawdz(vfs_przygotuj_standardowe(3) == VFS_OK, "std fd procesu 3");
    sprawdz(vfs_przygotuj_standardowe(4) == VFS_OK, "std fd procesu 4");

    VfsWynik blad = VFS_EIO;
    const int32_t fd = vfs_zarezerwuj(3, VFS_PLIK, "/tajne/dane.txt", 15,
                                      VFS_O_RDONLY, &blad);
    sprawdz(fd == 3, "proces 3 otwiera fd 3");

    /*
     * Izolacja jest STRUKTURALNA: tablica jest dwuwymiarowa [pid][fd],
     * wiec deskryptor procesu 3 lezy w innym wierszu niz ten sam numer
     * procesu 4. Ten test sprawdza, ze wiersze sa realnie niezalezne.
     *
     * Uwaga metodyczna: samo "proces 4 nie widzi fd 3" przechodzi trywialnie,
     * bo slot (4,3) jest po prostu pusty. Dlatego nizej zapelniamy cala
     * tablice procesu 3 i sprawdzamy, ze proces 4 nadal widzi tylko swoje -
     * to jest wlasciwy test niezaleznosci wierszy.
     */
    sprawdz(vfs_pobierz(4, fd) == nullptr,
            "proces 4 nie widzi pustego slotu procesu 3");
    sprawdz(vfs_zamknij(4, fd) == VFS_EBADF,
            "proces 4 nie moze zamknac fd procesu 3");

    /* Proces 4 ma wlasny fd 3 - niezalezny od procesu 3. */
    const int32_t fd4 = vfs_zarezerwuj(4, VFS_PLIK, "/inne.txt", 9,
                                       VFS_O_WRONLY, &blad);
    sprawdz(fd4 == 3, "proces 4 dostaje wlasny fd 3");
    VfsDeskryptor* d3 = vfs_pobierz(3, 3);
    VfsDeskryptor* d4 = vfs_pobierz(4, 3);
    sprawdz(d3 && d4, "oba deskryptory istnieja");
    if (d3 && d4) {
        sprawdz(std::strcmp(d3->sciezka, "/tajne/dane.txt") == 0,
                "deskryptor procesu 3 nietkniety");
        sprawdz(std::strcmp(d4->sciezka, "/inne.txt") == 0,
                "deskryptor procesu 4 wlasny");
        /*
         * Rozne obiekty w pamieci: gdyby tablica byla wspolna (1D po fd),
         * te dwa wskazniki bylyby identyczne i zapis procesu 4 niszczylby
         * deskryptor procesu 3.
         */
        sprawdz(d3 != d4, "deskryptory to rozne obiekty, nie wspolny slot");
    }

    /*
     * Zapelniamy CALY wiersz procesu 3 i sprawdzamy, ze proces 4 nie
     * zyskuje ani jednego widocznego deskryptora. To jest test, ktory
     * zawiedzie, gdyby ktos zmienil tablice na wspolna.
     */
    for (uint32_t i = 0; i < VFS_MAKS_DESKRYPTOROW; ++i) {
        (void)vfs_zarezerwuj(3, VFS_PLIK, "/zapelnienie", 12, VFS_O_RDONLY, &blad);
    }
    const uint32_t otwarte_3 = vfs_liczba_otwartych(3);
    const uint32_t otwarte_4 = vfs_liczba_otwartych(4);
    sprawdz(otwarte_3 == VFS_MAKS_DESKRYPTOROW,
            "proces 3 ma pelna tablice (64)");
    sprawdz(otwarte_4 == 4,
            "proces 4 nadal ma tylko swoje 4 deskryptory mimo pelnej tablicy 3");

    /*
     * Deskryptor procesu 4 wskazuje nadal jego plik, a nie ostatni plik
     * procesu 3 - to dowod, ze zapelnienie cudzego wiersza nie przecieka.
     */
    VfsDeskryptor* d4_po = vfs_pobierz(4, 3);
    sprawdz(d4_po != nullptr, "fd 3 procesu 4 istnieje po zapelnieniu tablicy 3");
    if (d4_po) {
        sprawdz(std::strcmp(d4_po->sciezka, "/inne.txt") == 0,
                "fd 3 procesu 4 nie zostal nadpisany przez proces 3");
    }
}

/* =========================================================================
 * 4. RECYKLING PID
 * ========================================================================= */

void test_recykling() {
    vfs_tabela_inicjalizuj();
    sprawdz(vfs_przygotuj_standardowe(5) == VFS_OK, "std fd procesu 5");

    VfsWynik blad = VFS_EIO;
    for (int i = 0; i < 5; ++i) {
        sprawdz(vfs_zarezerwuj(5, VFS_PLIK, "/a/b/c.txt", 10, VFS_O_RDWR,
                               &blad) >= 0, "otwarcie pliku przed recyklingiem");
    }
    sprawdz(vfs_liczba_otwartych(5) == 8, "3 terminale + 5 plikow");

    /*
     * To jest krytyczny test: po zwolnieniu PID nowy proces musi dostac
     * czysta tablice. Inaczej moglby czytac pliki poprzednika przez fd.
     */
    const uint32_t zamkniete = vfs_tabela_zwolnij_proces(5);
    sprawdz(zamkniete == 8, "zwolniono wszystkie 8 deskryptorow");
    sprawdz(vfs_liczba_otwartych(5) == 0, "tablica PID 5 pusta");

    sprawdz(vfs_przygotuj_standardowe(5) == VFS_OK, "nowy proces na tym samym PID");
    sprawdz(vfs_liczba_otwartych(5) == 3, "nowy proces ma tylko 3 terminale");
    sprawdz(vfs_pobierz(5, 3) == nullptr, "brak plikow poprzednika");

    const int32_t fd = vfs_zarezerwuj(5, VFS_PLIK, "/nowy.txt", 9,
                                      VFS_O_RDONLY, &blad);
    sprawdz(fd == 3, "nowy proces dostaje fd 3 od zera");
}

/* =========================================================================
 * 5. LIMITY
 * ========================================================================= */

void test_limity() {
    vfs_tabela_inicjalizuj();
    sprawdz(vfs_przygotuj_standardowe(6) == VFS_OK, "std fd procesu 6");

    VfsWynik blad = VFS_EIO;
    /* Zajmujemy wszystkie wolne slotu od 3 do 63: 61 deskryptorow. */
    int otwarte = 0;
    for (uint32_t i = VFS_FD_STDERR + 1; i < VFS_MAKS_DESKRYPTOROW; ++i) {
        const int32_t fd = vfs_zarezerwuj(6, VFS_PLIK, "/f", 2, VFS_O_RDONLY, &blad);
        if (fd >= 0) ++otwarte;
    }
    sprawdz(otwarte == 61, "zajeto 61 wolnych slotow");
    sprawdz(vfs_liczba_otwartych(6) == 64, "tablica pelna (64)");

    /* Kolejne otwarcie musi zwrocic EMFILE, nie zasmiardzic slotu. */
    const int32_t brak = vfs_zarezerwuj(6, VFS_PLIK, "/g", 2, VFS_O_RDONLY, &blad);
    sprawdz(brak == -1, "brak miejsca zwraca -1");
    sprawdz(blad == VFS_EMFILE, "brak miejsca zwraca EMFILE");

    /* Po zamknieciu jednego slot znów jest dostepny. */
    sprawdz(vfs_zamknij(6, 10) == VFS_OK, "zamkniecie w pelnej tablicy");
    const int32_t znow = vfs_zarezerwuj(6, VFS_PLIK, "/h", 2, VFS_O_RDONLY, &blad);
    sprawdz(znow == 10, "zwolniony slot uzyty ponownie");

    /*
     * Sciezka dluzsza niz bufor musi byc odrzucona, nie obcieta: obciecie
     * wskazywaloby inny plik niz ten, ktory uzytkownik podal.
     */
    char dluga[VFS_MAKS_SCIEZKA + 8];
    for (size_t i = 0; i + 1 < sizeof(dluga); ++i) dluga[i] = 'a';
    dluga[0] = '/';
    dluga[sizeof(dluga) - 1] = '\0';
    const int32_t zla = vfs_zarezerwuj(1, VFS_PLIK, dluga, sizeof(dluga) - 1,
                                       VFS_O_RDONLY, &blad);
    sprawdz(zla == -1, "za dluga sciezka odrzucona");
    sprawdz(blad == VFS_ENAMETOOLONG, "za dluga sciezka -> ENAMETOOLONG");

    /* Pusta sciezka tez jest bledem, a nie "katalogiem biezacym". */
    sprawdz(vfs_zarezerwuj(1, VFS_PLIK, "", 0, VFS_O_RDONLY, &blad) == -1,
            "pusta sciezka odrzucona");
    sprawdz(blad == VFS_EINVAL, "pusta sciezka -> EINVAL");
}

/* =========================================================================
 * 6. WALIDACJA FLAG
 * ========================================================================= */

void test_flagi() {
    sprawdz(vfs_waliduj_flagi(VFS_O_RDONLY) == VFS_OK, "O_RDONLY poprawne");
    sprawdz(vfs_waliduj_flagi(VFS_O_WRONLY) == VFS_OK, "O_WRONLY poprawne");
    sprawdz(vfs_waliduj_flagi(VFS_O_RDWR) == VFS_OK, "O_RDWR poprawne");
    sprawdz(vfs_waliduj_flagi(VFS_O_RDWR | VFS_O_CREAT | VFS_O_TRUNC) == VFS_OK,
            "O_RDWR|CREATE|TRUNC poprawne");

    /* Tryb 3 (O_ACCMODE) nie jest zadnym poprawnym trybem. */
    sprawdz(vfs_waliduj_flagi(3) == VFS_EINVAL, "tryb 3 odrzucony");

    /*
     * O_TRUNC z O_RDONLY jest sprzeczne. Linux obcina plik mimo to; my
     * odmawiamy, bo cicha utrata danych nie jest zgodnoscia.
     */
    sprawdz(vfs_waliduj_flagi(VFS_O_RDONLY | VFS_O_TRUNC) == VFS_EINVAL,
            "TRUNC + RDONLY odrzucone");

    /* O_EXCL bez O_CREAT nie ma znaczenia. */
    sprawdz(vfs_waliduj_flagi(VFS_O_RDONLY | VFS_O_EXCL) == VFS_EINVAL,
            "EXCL bez CREATE odrzucone");
    sprawdz(vfs_waliduj_flagi(VFS_O_RDWR | VFS_O_EXCL | VFS_O_CREAT) == VFS_OK,
            "EXCL z CREATE poprawne");

    /* Niepoprawne flagi nie moga zarezerwowac deskryptora. */
    vfs_tabela_inicjalizuj();
    (void)vfs_przygotuj_standardowe(7);
    VfsWynik blad = VFS_OK;
    sprawdz(vfs_zarezerwuj(7, VFS_PLIK, "/x", 2, 3, &blad) == -1,
            "open z trybem 3 odrzucony");
    sprawdz(blad == VFS_EINVAL, "open z trybem 3 -> EINVAL");
}

/* =========================================================================
 * 7. PRAWA ODCZYTU I ZAPISU
 * ========================================================================= */

void test_prawa() {
    vfs_tabela_inicjalizuj();
    (void)vfs_przygotuj_standardowe(8);

    VfsWynik blad = VFS_OK;
    const int32_t ro = vfs_zarezerwuj(8, VFS_PLIK, "/ro", 3, VFS_O_RDONLY, &blad);
    const int32_t wo = vfs_zarezerwuj(8, VFS_PLIK, "/wo", 3, VFS_O_WRONLY, &blad);
    const int32_t rw = vfs_zarezerwuj(8, VFS_PLIK, "/rw", 3, VFS_O_RDWR, &blad);

    const VfsDeskryptor* d_ro = vfs_pobierz(8, ro);
    const VfsDeskryptor* d_wo = vfs_pobierz(8, wo);
    const VfsDeskryptor* d_rw = vfs_pobierz(8, rw);

    sprawdz(vfs_moze_odczytac(d_ro) && !vfs_moze_zapisac(d_ro), "RDONLY: R tak, W nie");
    sprawdz(!vfs_moze_odczytac(d_wo) && vfs_moze_zapisac(d_wo), "WRONLY: R nie, W tak");
    sprawdz(vfs_moze_odczytac(d_rw) && vfs_moze_zapisac(d_rw), "RDWR: R i W tak");

    /* nullptr nie moze byc interpretowany jako "wszystko wolno". */
    sprawdz(!vfs_moze_odczytac(nullptr), "nullptr: brak odczytu");
    sprawdz(!vfs_moze_zapisac(nullptr), "nullptr: brak zapisu");
}

/* =========================================================================
 * 8. ABI VfsStat
 * ========================================================================= */

void test_stat_abi() {
    sprawdz(sizeof(VfsStat) == 144, "VfsStat ma 144 B jak Linux x86-64");

    VfsStat st{};
    st.st_size = 12345;
    st.st_mode = VFS_TRYB_PLIK;
    sprawdz(st.st_size == 12345, "pole st_size zapisywalne");

    /*
     * Offsety pol sa czescia ABI: kazdy naglowek <sys/stat.h> w obcym libc
     * zaklada dokladnie te pozycje. Zla kolejnosc nie daje bledu kompilacji,
     * tylko smieci w aplikacji, wiec sprawdzamy ja wprost.
     */
    sprawdz(offsetof(VfsStat, st_dev) == 0, "offset st_dev");
    sprawdz(offsetof(VfsStat, st_ino) == 8, "offset st_ino");
    sprawdz(offsetof(VfsStat, st_nlink) == 16, "offset st_nlink");
    sprawdz(offsetof(VfsStat, st_mode) == 24, "offset st_mode");
    sprawdz(offsetof(VfsStat, st_size) == 48, "offset st_size");
    sprawdz(offsetof(VfsStat, st_blksize) == 56, "offset st_blksize");
    sprawdz(offsetof(VfsStat, st_blocks) == 64, "offset st_blocks");

    /* Tryby plikow musza miec poprawne bity typu. */
    sprawdz((VFS_TRYB_PLIK & VFS_S_IFMT) == VFS_S_IFREG, "tryb pliku = S_IFREG");
    sprawdz((VFS_TRYB_KATALOG & VFS_S_IFMT) == VFS_S_IFDIR, "tryb katalogu = S_IFDIR");
    sprawdz((VFS_TRYB_PLIK & 0x1FFU) == 0644, "plik ma uprawnienia 0644");
    sprawdz((VFS_TRYB_KATALOG & 0x1FFU) == 0755, "katalog ma uprawnienia 0755");
}

/* =========================================================================
 * 9. ZLE ARGUMENTY
 * ========================================================================= */

void test_zle_argumenty() {
    vfs_tabela_inicjalizuj();

    /* PID 0 to jadro i nie ma prawa dostac deskryptorow uzytkownika. */
    VfsWynik blad = VFS_OK;
    sprawdz(vfs_zarezerwuj(0, VFS_PLIK, "/x", 2, VFS_O_RDONLY, &blad) == -1,
            "PID 0 odrzucony");
    sprawdz(vfs_pobierz(0, 0) == nullptr, "PID 0 nie ma deskryptorow");

    /* PID poza zakresem nie moze wyjsc poza tablice. */
    sprawdz(vfs_zarezerwuj(VFS_MAKS_PROCESOW, VFS_PLIK, "/x", 2,
                           VFS_O_RDONLY, &blad) == -1,
            "PID poza zakresem odrzucony");
    sprawdz(vfs_pobierz(VFS_MAKS_PROCESOW, 3) == nullptr,
            "PID poza zakresem: brak deskryptora");

    (void)vfs_przygotuj_standardowe(9);

    /* Ujemne fd i fd poza zakresem. */
    sprawdz(vfs_pobierz(9, -1) == nullptr, "fd -1 odrzucony");
    sprawdz(vfs_pobierz(9, 64) == nullptr, "fd 64 odrzucony");
    sprawdz(vfs_zamknij(9, -5) == VFS_EBADF, "zamkniecie fd -5 -> EBADF");

    /* nullptr jako sciezka. */
    sprawdz(vfs_zarezerwuj(9, VFS_PLIK, nullptr, 5, VFS_O_RDONLY, &blad) == -1,
            "nullptr jako sciezka odrzucony");
    sprawdz(blad == VFS_EINVAL, "nullptr jako sciezka -> EINVAL");

    /* Nieznany rodzaj deskryptora nie moze byc przydzielony. */
    sprawdz(vfs_zarezerwuj(9, VFS_WOLNY, "/x", 2, VFS_O_RDONLY, &blad) == -1,
            "rodzaj WOLNY odrzucony jako cel open");
    sprawdz(vfs_zarezerwuj(9, VFS_TERMINAL, "/x", 2, VFS_O_RDONLY, &blad) == -1,
            "rodzaj TERMINAL odrzucony jako cel open");

    /* Opis kazdego wyniku jest niepusty - logi nie moga byc puste. */
    const VfsWynik wszystkie[] = {
        VFS_OK, VFS_EBADF, VFS_EMFILE, VFS_EINVAL, VFS_EACCES,
        VFS_ENAMETOOLONG, VFS_EISDIR, VFS_ENOTDIR, VFS_ENOENT,
        VFS_EEXIST, VFS_ENOSPC, VFS_EIO, VFS_EROFS, VFS_EWRONLY
    };
    for (VfsWynik w : wszystkie) {
        sprawdz(vfs_wynik_opis(w) != nullptr && vfs_wynik_opis(w)[0] != '\0',
                "kazdy wynik ma opis");
    }
}

}  // namespace

int main() {
    /*
     * Alokator dla testu: malloc/free. Wewnatrz jadra vfs_jadro_inicjalizuj()
     * podaje kmalloc/kfree; tutaj potrzebujemy odpowiednika, ktory nie
     * wymaga linkowania heapu jadra.
     */
    vfs_ustaw_alokator(
        [](size_t n) -> void* { return std::malloc(n); },
        [](void* p) { std::free(p); });

    test_przydzial();
    test_socket_fd_izolacja();
    test_standardowe();
    test_izolacja();
    test_recykling();
    test_limity();
    test_flagi();
    test_prawa();
    test_stat_abi();
    test_zle_argumenty();

    /*
     * Sprzatanie: kazdy PID, ktory cokolwiek alokowal, musi zostac zwolniony.
     * ASan wykrylby wyciek, gdyby ktoras sciezka nie zwalniala deskryptora -
     * dokladnie taki sam wyciek w jadrze oznaczalby, ze proces zuzywa pamiec
     * mimo zamkniecia plikow.
     */
    for (uint32_t pid = 1; pid < VFS_MAKS_PROCESOW; ++pid) {
        (void)vfs_tabela_zwolnij_proces(pid);
    }
    sprawdz(vfs_liczba_otwartych(1) == 0, "po sprzataniu PID 1 pusty");
    sprawdz(vfs_liczba_otwartych(6) == 0, "po sprzataniu PID 6 pusty");

    if (g_niepowodzenia != 0) {
        std::printf("[VFS-HOST] FAIL %d/%d\n", g_niepowodzenia, g_sprawdzenia);
        return 1;
    }
    std::printf("[VFS-HOST] PASS %d asercji\n", g_sprawdzenia);
    return 0;
}
