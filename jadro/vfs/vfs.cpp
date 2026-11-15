#include "vfs.h"

/*
 * Bursztyn OS - VFS, czysta logika tablicy deskryptorow.
 *
 * Plik nie zawiera zadnej zaleznosci od jadra, PSF ani alokatora, wiec
 * kompiluje sie zarowno do jadra (-mno-sse, bez libc), jak i do testu
 * hosta pod ASan/UBSan. Cala polityka deskryptorow jest testowana bez QEMU.
 *
 * Pamiec: tablica jest tablica WSKAZNIKOW (8 KiB), a deskryptor (~280 B)
 * jest alokowany przy open() i zwalniany przy close(). Statyczna tablica
 * struktur zajmowalaby 280 KiB BSS, a obraz jadra konczy sie dokladnie
 * na limicie linkera - kazdy kilobajt BSS jest realnym kosztem.
 *
 * Alokator jest WSTRZYKIWANY (vfs_ustaw_alokator), zeby test hosta uzywal
 * malloc/free bez linkowania jadrowego heapu. Wartosc domyslna to nullptr,
 * co oznacza "alokator nieustawiony" i kazde open() zwraca EMFILE - jawna
 * odmowa zamiast dereferencji pustego wskaznika.
 */

namespace {

enum : uint32_t {
    /* Znaczniki stanu slotu. Wartosci sa celowo rozne od VfsRodzaj. */
    SLOT_WOLNY = 0xFFFFFFFFU,
    SLOT_TERMINAL = 0xFFFFFFFEU
};

/*
 * Wskaznik na deskryptor albo znacznik. Rozdzielenie "brak wpisu" od
 * "wpis terminala" jest konieczne: terminale 0/1/2 nie maja alokowanego
 * deskryptora (sa stale), a ich zamkniecie musi nadal zmieniac stan.
 */
inline bool slot_zajety(const void* p) {
    return p != nullptr;
}

inline void* slot_terminal() {
    return reinterpret_cast<void*>(
        static_cast<uintptr_t>(SLOT_TERMINAL));
}

inline bool slot_to_terminal(const void* p) {
    return reinterpret_cast<uintptr_t>(p) == static_cast<uintptr_t>(SLOT_TERMINAL);
}

/*
 * Stan terminala. NIE mozna uzywac flag jako znacznika otwarcia:
 * VFS_O_RDONLY == 0, wiec stdin otwarty tylko do czytania mialby flagi 0
 * i wygladalby jak zamkniety. Dlatego osobna tablica bool.
 */
VfsRodzaj g_terminal_rodzaj[VFS_MAKS_PROCESOW][3];
uint32_t g_terminal_flagi[VFS_MAKS_PROCESOW][3];
bool g_terminal_otwarty[VFS_MAKS_PROCESOW][3];

/*
 * Jedna tablica wskaznikow dla wszystkich procesow, indeksowana PID-em.
 *
 * Alternatywa bylaby tablica per proces_t, ale to zmiana ABI schedulera
 * (static_asserty na offsety w kilku modulach). Tablica wyspecjalizowana
 * jest izolowana, nie zmienia proces_t i daje ten sam efekt bezpieczenstwa
 * dzieki sprawdzaniu PID wlasciciela przy kazdej operacji.
 */
void* g_tabela[VFS_MAKS_PROCESOW][VFS_MAKS_DESKRYPTOROW];

VfsAlokator g_alokuj = nullptr;
VfsZwolnij g_zwolnij = nullptr;

bool g_zainicjalizowana = false;

/* Zwalnia jeden slot, jesli jest deskryptorem pliku/katalogu. */
void zwolnij_slot(void* p);

}  // namespace

void vfs_ustaw_alokator(VfsAlokator alokuj, VfsZwolnij zwolnij) {
    g_alokuj = alokuj;
    g_zwolnij = zwolnij;
}

const char* vfs_wynik_opis(VfsWynik wynik) {
    switch (wynik) {
        case VFS_OK: return "ok";
        case VFS_EBADF: return "bad-file-descriptor";
        case VFS_EMFILE: return "too-many-open-files";
        case VFS_EINVAL: return "invalid-argument";
        case VFS_EACCES: return "permission-denied";
        case VFS_ENAMETOOLONG: return "path-too-long";
        case VFS_EISDIR: return "is-a-directory";
        case VFS_ENOTDIR: return "not-a-directory";
        case VFS_ENOENT: return "no-such-file";
        case VFS_EEXIST: return "file-exists";
        case VFS_ENOSPC: return "no-space";
        case VFS_EIO: return "io-error";
        case VFS_EROFS: return "read-only";
        case VFS_EWRONLY: return "write-only";
        default: return "unknown";
    }
}

void vfs_tabela_inicjalizuj() {
    /*
     * Reinicjalizacja MUSI zwolnic to, co juz wisi w tablicy. Zerowanie
     * wskaznikow bez zwolnienia wycieka kazdy otwarty deskryptor - to samo
     * w jadrze oznaczaloby trwala utrate pamieci przy ponownym starcie
     * podsystemu. Ta sciezka jest wykonana raz, ale musi byc poprawna.
     */
    for (uint32_t p = 0; p < VFS_MAKS_PROCESOW; ++p) {
        for (uint32_t f = 0; f < VFS_MAKS_DESKRYPTOROW; ++f) {
            zwolnij_slot(g_tabela[p][f]);
            g_tabela[p][f] = nullptr;
        }
        for (uint32_t f = 0; f < 3; ++f) {
            g_terminal_rodzaj[p][f] = VFS_WOLNY;
            g_terminal_flagi[p][f] = 0;
            g_terminal_otwarty[p][f] = false;
        }
    }
    g_zainicjalizowana = true;
}

namespace {

/* Zwalnia jeden slot, jesli jest deskryptorem pliku/katalogu. */
void zwolnij_slot(void* p) {
    if (!slot_zajety(p) || slot_to_terminal(p)) return;
    if (g_zwolnij) g_zwolnij(p);
}

}  // namespace
uint32_t vfs_tabela_zwolnij_proces(uint32_t pid) {
    if (pid == 0 || pid >= VFS_MAKS_PROCESOW) return 0;

    uint32_t zamkniete = 0;
    for (uint32_t f = 0; f < VFS_MAKS_DESKRYPTOROW; ++f) {
        void*& p = g_tabela[pid][f];
        if (!slot_zajety(p)) continue;
        if (slot_to_terminal(p)) {
            /* Terminal: liczymy jako zamkniety tylko gdy byl otwarty. */
            if (f < 3 && g_terminal_otwarty[pid][f]) ++zamkniete;
            g_terminal_rodzaj[pid][f] = VFS_WOLNY;
            g_terminal_flagi[pid][f] = 0;
            g_terminal_otwarty[pid][f] = false;
        } else {
            ++zamkniete;
            zwolnij_slot(p);
        }
        p = nullptr;
    }
    return zamkniete;
}

VfsWynik vfs_przygotuj_standardowe(uint32_t pid) {
    if (pid == 0 || pid >= VFS_MAKS_PROCESOW) return VFS_EBADF;

    for (uint32_t fd = 0; fd < 3; ++fd) {
        void*& p = g_tabela[pid][fd];
        /*
         * Deskryptor moze byc juz przygotowany (np. ponowne wywolanie po
         * nieudanym uruchomieniu). Nie nadpisujemy go wtedy, bo moglby
         * zgubic stan.
         */
        if (slot_zajety(p) && slot_to_terminal(p) &&
            g_terminal_rodzaj[pid][fd] == VFS_TERMINAL) {
            continue;
        }
        /* Stary, nie-terminalny wpis w slocie 0/1/2 nie moze zostac. */
        zwolnij_slot(p);

        p = slot_terminal();
        g_terminal_rodzaj[pid][fd] = VFS_TERMINAL;
        g_terminal_flagi[pid][fd] =
            (fd == VFS_FD_STDIN) ? VFS_O_RDONLY : VFS_O_WRONLY;
        g_terminal_otwarty[pid][fd] = true;
    }
    return VFS_OK;
}

VfsWynik vfs_waliduj_flagi(uint32_t flagi) {
    const uint32_t tryb = flagi & VFS_O_ACCMODE;
    if (tryb != VFS_O_RDONLY && tryb != VFS_O_WRONLY && tryb != VFS_O_RDWR) {
        return VFS_EINVAL;
    }

    /* O_EXCL bez O_CREAT nie ma znaczenia i jest bledem (tak jak w Linuksie). */
    if ((flagi & VFS_O_EXCL) && !(flagi & VFS_O_CREAT)) return VFS_EINVAL;

    /*
     * O_TRUNC z O_RDONLY jest sprzeczne: nie mozna obciac pliku, ktorego
     * nie wolno zapisac. Linux to przyjmuje i obcina, my odmawiamy - cicha
     * utrata danych uzytkownika nie jest "zgodnoscia", tylko bledem.
     */
    if ((flagi & VFS_O_TRUNC) && tryb == VFS_O_RDONLY) return VFS_EINVAL;

    return VFS_OK;
}

int32_t vfs_zarezerwuj(uint32_t pid, VfsRodzaj rodzaj, const char* sciezka,
                       size_t dlugosc_sciezki, uint32_t flagi,
                       VfsWynik* blad) {
    if (blad) *blad = VFS_OK;
    if (!g_zainicjalizowana || pid == 0 || pid >= VFS_MAKS_PROCESOW) {
        if (blad) *blad = VFS_EBADF;
        return -1;
    }
    if (rodzaj != VFS_GNIAZDO && (!sciezka || dlugosc_sciezki == 0)) {
        if (blad) *blad = VFS_EINVAL;
        return -1;
    }
    /*
     * Sciezka dluzsza niz bufor jest odrzucana, nie obcinana. Obciecie
     * wskazywaloby na inny plik niz ten, ktory uzytkownik otworzyl.
     */
    if (rodzaj != VFS_GNIAZDO &&
        dlugosc_sciezki + 1 > VFS_MAKS_SCIEZKA) {
        if (blad) *blad = VFS_ENAMETOOLONG;
        return -1;
    }
    if (rodzaj != VFS_PLIK && rodzaj != VFS_KATALOG &&
        rodzaj != VFS_GNIAZDO) {
        if (blad) *blad = VFS_EINVAL;
        return -1;
    }
    if (rodzaj == VFS_GNIAZDO &&
        (sciezka != nullptr || dlugosc_sciezki != 0)) {
        if (blad) *blad = VFS_EINVAL;
        return -1;
    }
    if (!g_alokuj || !g_zwolnij) {
        if (blad) *blad = VFS_EMFILE;
        return -1;
    }

    const VfsWynik flagi_ok = vfs_waliduj_flagi(flagi);
    if (flagi_ok != VFS_OK) {
        if (blad) *blad = flagi_ok;
        return -1;
    }

    /*
     * Szukamy pierwszego wolnego slotu od 3 w gore: 0/1/2 sa terminalem
     * i nie wolno ich przydzielic plikowi, bo zapis "na stdout" zaczalby
     * pisac do przypadkowego pliku.
     */
    for (uint32_t fd = VFS_FD_STDERR + 1; fd < VFS_MAKS_DESKRYPTOROW; ++fd) {
        void*& p = g_tabela[pid][fd];
        if (slot_zajety(p)) continue;

        void* surowy = g_alokuj(sizeof(VfsDeskryptor));
        if (!surowy) {
            if (blad) *blad = VFS_ENOSPC;
            return -1;
        }

        VfsDeskryptor* d = static_cast<VfsDeskryptor*>(surowy);
        d->rodzaj = rodzaj;
        d->pid = pid;
        d->flagi = flagi;
        d->przesuniecie = 0;
        for (size_t i = 0; i < dlugosc_sciezki; ++i) d->sciezka[i] = sciezka[i];
        d->sciezka[dlugosc_sciezki] = '\0';

        p = d;
        return static_cast<int32_t>(fd);
    }

    if (blad) *blad = VFS_EMFILE;
    return -1;
}

VfsDeskryptor* vfs_pobierz(uint32_t pid, int32_t fd) {
    if (!g_zainicjalizowana) return nullptr;
    if (pid == 0 || pid >= VFS_MAKS_PROCESOW) return nullptr;
    if (fd < 0 || fd >= static_cast<int32_t>(VFS_MAKS_DESKRYPTOROW)) return nullptr;

    void* p = g_tabela[pid][fd];
    if (!slot_zajety(p) || slot_to_terminal(p)) return nullptr;

    VfsDeskryptor* d = static_cast<VfsDeskryptor*>(p);
    /*
     * Sprawdzenie wlasciciela jest kluczowe: numer fd jest maly i zgadywalny,
     * wiec bez tego proces moglby czytac pliki innego procesu, podajac tylko
     * jego numer deskryptora.
     */
    if (d->pid != pid) return nullptr;
    return d;
}

VfsWynik vfs_zamknij(uint32_t pid, int32_t fd) {
    if (!g_zainicjalizowana) return VFS_EBADF;
    if (pid == 0 || pid >= VFS_MAKS_PROCESOW) return VFS_EBADF;
    if (fd < 0 || fd >= static_cast<int32_t>(VFS_MAKS_DESKRYPTOROW)) return VFS_EBADF;

    void*& p = g_tabela[pid][fd];
    if (!slot_zajety(p)) return VFS_EBADF;

    if (slot_to_terminal(p)) {
        /*
         * Standardowe deskryptory mozna zamknac (POSIX na to pozwala, a musl
         * tego uzywa przy redirekcji), ale nie wolno ich "zwolnic" w sposob,
         * ktory pozwolilby przydzielic ten numer plikowi. Dlatego zamkniecie
         * 0/1/2 oznacza tylko zmiane flag na 0 (brak dostepu) i pozostawienie
         * znacznika terminala - slot zostaje zajety.
         */
        if (fd < 3) {
            g_terminal_flagi[pid][fd] = 0;
            g_terminal_otwarty[pid][fd] = false;
        }
        return VFS_OK;
    }

    zwolnij_slot(p);
    p = nullptr;
    return VFS_OK;
}

bool vfs_moze_odczytac(const VfsDeskryptor* d) {
    if (!d || d->rodzaj == VFS_WOLNY) return false;
    const uint32_t tryb = d->flagi & VFS_O_ACCMODE;
    return tryb == VFS_O_RDONLY || tryb == VFS_O_RDWR;
}

bool vfs_moze_zapisac(const VfsDeskryptor* d) {
    if (!d || d->rodzaj == VFS_WOLNY) return false;
    const uint32_t tryb = d->flagi & VFS_O_ACCMODE;
    return tryb == VFS_O_WRONLY || tryb == VFS_O_RDWR;
}

bool vfs_to_terminal(uint32_t pid, int32_t fd) {
    if (!g_zainicjalizowana) return false;
    if (pid == 0 || pid >= VFS_MAKS_PROCESOW) return false;
    if (fd < 0 || fd >= static_cast<int32_t>(VFS_MAKS_DESKRYPTOROW)) return false;
    const void* p = g_tabela[pid][fd];
    return slot_zajety(p) && slot_to_terminal(p);
}

uint32_t vfs_flagi_terminala(uint32_t pid, int32_t fd) {
    if (pid == 0 || pid >= VFS_MAKS_PROCESOW) return 0;
    if (fd < 0 || fd >= 3) return 0;
    if (!vfs_to_terminal(pid, fd)) return 0;
    if (!g_terminal_otwarty[pid][fd]) return 0;
    return g_terminal_flagi[pid][fd];
}

uint32_t vfs_liczba_otwartych(uint32_t pid) {
    if (pid == 0 || pid >= VFS_MAKS_PROCESOW) return 0;
    uint32_t liczba = 0;
    for (uint32_t f = 0; f < VFS_MAKS_DESKRYPTOROW; ++f) {
        if (slot_zajety(g_tabela[pid][f])) ++liczba;
    }
    return liczba;
}

namespace {

bool vfs_zapisz_le(void* bufor, size_t pojemnosc, size_t offset,
                   uint64_t wartosc, size_t bajty) {
    if (!bufor || bajty == 0 || bajty > sizeof(uint64_t) ||
        offset > pojemnosc || bajty > pojemnosc - offset) {
        return false;
    }
    uint8_t* cel = static_cast<uint8_t*>(bufor) + offset;
    for (size_t i = 0; i < bajty; ++i) {
        cel[i] = static_cast<uint8_t>(wartosc & 0xFFU);
        wartosc >>= 8;
    }
    return true;
}

}  // namespace

bool vfs_zbuduj_linux_dirent64(
    void* bufor,
    size_t pojemnosc,
    uint64_t inode,
    uint64_t nastepny_offset,
    uint8_t typ,
    const char* nazwa,
    size_t nazwa_pojemnosc,
    size_t* zapisano) {
    if (zapisano) *zapisano = 0;
    if (!bufor || !nazwa || !zapisano || nazwa_pojemnosc == 0 ||
        nastepny_offset > UINT64_C(0x7FFFFFFFFFFFFFFF)) {
        return false;
    }

    size_t dlugosc = 0;
    while (dlugosc < nazwa_pojemnosc && nazwa[dlugosc] != '\0') ++dlugosc;
    if (dlugosc == 0 || dlugosc == nazwa_pojemnosc) return false;

    constexpr size_t naglowek = 19;  /* d_name zaczyna sie pod offsetem 19. */
    const size_t surowy = naglowek + dlugosc + 1;
    if (surowy < naglowek || surowy > SIZE_MAX - 7) return false;
    const size_t rekord = (surowy + 7) & ~static_cast<size_t>(7);
    if (rekord > UINT16_MAX || rekord > pojemnosc) return false;

    uint8_t* cel = static_cast<uint8_t*>(bufor);
    for (size_t i = 0; i < rekord; ++i) cel[i] = 0;
    if (!vfs_zapisz_le(cel, rekord, 0, inode, sizeof(uint64_t)) ||
        !vfs_zapisz_le(cel, rekord, 8, nastepny_offset, sizeof(uint64_t)) ||
        !vfs_zapisz_le(cel, rekord, 16, rekord, sizeof(uint16_t))) {
        return false;
    }
    cel[18] = typ;
    for (size_t i = 0; i <= dlugosc; ++i) cel[naglowek + i] =
        (i == dlugosc) ? '\0' : static_cast<uint8_t>(nazwa[i]);

    *zapisano = rekord;
    return true;
}
