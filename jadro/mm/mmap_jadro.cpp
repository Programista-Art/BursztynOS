/*
 * Bursztyn OS - mmap w jadrze.
 *
 * Mapowanie jest EAGER: kazda strona dostaje ramke od razu, przez
 * ZaalokujRamke() i VmmMapujNowaStroneProcesu(). Wybor jest swiadomy:
 *
 *   - nie zmieniamy modelu VMM (demand paging wymaga obslugi #PF i liczenia
 *     RSS, co jest osobnym, wiekszym punktem),
 *   - kazda zmapowana strona jest natychmiast sprawdzalna przez
 *     bws_vmm_sprawdz_zakres_uzytkownika(), wiec walidacja BWS dziala bez
 *     zmian,
 *   - brak COW oznacza, ze MAP_PRIVATE i MAP_SHARED sa rownowazne dopoki
 *     nie ma fork() ani wspolnych plikow. To jest jawnie udokumentowane.
 *
 * Konwencja bledow w tym pliku jest JEDNA i negatywna: kazdy blad to
 * -errno (np. -12 = -ENOMEM). Funkcje nie neguja wartosci ponownie, zeby
 * nie bylo miejsca na podwojna negacje - to byl realny blad: mmap z
 * PROT_WRITE|PROT_EXEC zwracal dodatnie 13 zamiast -EACCES i wygladal
 * jak poprawny adres.
 */
#include "mmap_jadro.h"
#include "pamiec.h"
#include "pzb.h"

namespace {

constexpr uint64_t STRONA = 4096ULL;
constexpr int64_t EINVAL = -22;
constexpr int64_t ENOMEM = -12;
constexpr int64_t ENOSYS = -38;
constexpr int64_t EACCES = -13;

StanMmapProcesu stany[PZB_MAKS_PROCESOW] = {};

inline bool pid_poprawny(int pid) {
    return pid > 0 && pid < static_cast<int>(PZB_MAKS_PROCESOW);
}

uint64_t zaokraglij_w_gore(uint64_t v) {
    return (v + STRONA - 1ULL) & ~(STRONA - 1ULL);
}

/*
 * Mapuje jedna strone. Zwraca false przy braku ramki albo bledzie VMM.
 * Ramka jest zerowana przez PMM (Faza 0.3), wiec nie ma wycieku danych
 * poprzedniego procesu.
 */
bool zmapuj_strone(uint64_t adres, uint64_t ochrona) {
    void* ramka = ZaalokujRamke();
    if (!ramka) return false;

    const bool zapis = (ochrona & POSIX_PROT_WRITE) != 0;
    const bool wykonuj = (ochrona & POSIX_PROT_EXEC) != 0;
    if (!VmmMapujNowaStroneProcesu(reinterpret_cast<void*>(adres), ramka,
                                   zapis, wykonuj)) {
        ZwolnijRamke(ramka);
        return false;
    }
    return true;
}

void odmapuj_strone(uint64_t adres) {
    VmmOdmapujStroneWspoldzielona(reinterpret_cast<void*>(adres));
}

/* Tlumaczy wynik mmap_waliduj_flagi (0 albo -errno) na nasze kody. */
int64_t blad_flag(uint64_t flagi, uint64_t ochrona) {
    const int64_t wynik = mmap_waliduj_flagi(flagi, ochrona);
    if (wynik == 0) return 0;
    /* -13 (EACCES) i -22 (EINVAL) sa juz wlasciwymi wartosciami ABI. */
    return wynik;
}

}  // namespace

void mmap_zwolnij_proces(int pid) {
    if (!pid_poprawny(pid)) return;
    StanMmapProcesu& s = stany[pid];
    for (uint32_t i = 0; i < MMAP_MAKS_REGIONOW; ++i) {
        /*
         * Ramki stron sa zwalniane przez VMM przy usunieciu procesu; tutaj
         * zdejmujemy tylko ksiegowanie, zeby nowy proces nie odziedziczyl
         * cudzych adresow.
         */
        s.regiony[i] = {};
    }
    s.liczba = 0;
}

const StanMmapProcesu* mmap_stan(int pid) {
    if (!pid_poprawny(pid)) return nullptr;
    return &stany[pid];
}

uint64_t mmap_jadro_mapuj(int pid, uint64_t adres, uint64_t dlugosc,
                          uint64_t ochrona, uint64_t flagi, int64_t fd,
                          uint64_t offset, int64_t* bledy) {
    int64_t blad = 0;
    uint64_t wynik = 0;

    do {
        if (!pid_poprawny(pid)) { blad = EINVAL; break; }
        StanMmapProcesu& s = stany[pid];

        /* Tylko mapowanie anonimowe; plikowe wymaga PSF i page cache. */
        if ((flagi & POSIX_MAP_ANONYMOUS) == 0) { blad = ENOSYS; break; }
        if (fd != -1) { blad = ENOSYS; break; }
        if (offset != 0) { blad = EINVAL; break; }

        blad = blad_flag(flagi, ochrona);
        if (blad) break;

        if (dlugosc == 0) { blad = EINVAL; break; }
        if (dlugosc > MMAP_MAKS_DLUGOSC) { blad = ENOMEM; break; }
        const uint64_t rozmiar = zaokraglij_w_gore(dlugosc);
        if (rozmiar < dlugosc) { blad = ENOMEM; break; }
        if (rozmiar > RING3_MMAP_ROZMIAR) { blad = ENOMEM; break; }
        /* Okno jest ciagle, wiec rozmiar musi byc wielokrotnoscia strony. */
        if ((dlugosc & (STRONA - 1ULL)) != 0) { blad = EINVAL; break; }

        uint32_t indeks = MMAP_MAKS_REGIONOW;
        for (uint32_t i = 0; i < MMAP_MAKS_REGIONOW; ++i) {
            if (!s.regiony[i].uzywany) { indeks = i; break; }
        }
        if (indeks == MMAP_MAKS_REGIONOW) { blad = ENOMEM; break; }

        uint64_t adres_docelowy;
        if (flagi & POSIX_MAP_FIXED) {
            /*
             * MAP_FIXED wymaga wyrownania i musi lezec w calosci w oknie.
             * Nie pozwalamy przykryc istniejacego mapowania - to odroznia
             * nas od Linuksa celowo, bo cicha podmiana mapowania jest
             * klasycznym zrodlem UAF w kodzie uzytkownika.
             */
            if (!mmap_adres_poprawny(adres & ~(STRONA - 1ULL), rozmiar)) {
                blad = EINVAL;
                break;
            }
            adres_docelowy = adres & ~(STRONA - 1ULL);
            if (mmap_znajdz_region(s.regiony, MMAP_MAKS_REGIONOW, adres_docelowy,
                                   rozmiar) >= 0) {
                blad = EINVAL;
                break;
            }
        } else {
            adres_docelowy = mmap_znajdz_miejsce(s.regiony, MMAP_MAKS_REGIONOW, rozmiar);
            if (adres_docelowy == 0) { blad = ENOMEM; break; }
        }

        /* Mapowanie strony po stronie; przy bledzie wycofujemy wszystko. */
        const uint64_t strony = rozmiar / STRONA;
        uint64_t zmapowane = 0;
        bool ok = true;
        for (; zmapowane < strony; ++zmapowane) {
            if (!zmapuj_strone(adres_docelowy + zmapowane * STRONA, ochrona)) {
                ok = false;
                break;
            }
        }
        if (!ok) {
            for (uint64_t i = 0; i < zmapowane; ++i)
                odmapuj_strone(adres_docelowy + i * STRONA);
            blad = ENOMEM;
            break;
        }

        s.regiony[indeks].adres = adres_docelowy;
        s.regiony[indeks].dlugosc = rozmiar;
        s.regiony[indeks].ochrona = static_cast<uint32_t>(ochrona);
        s.regiony[indeks].uzywany = true;
        ++s.liczba;
        wynik = adres_docelowy;
    } while (false);

    if (bledy) *bledy = blad;
    return wynik;
}

int64_t mmap_jadro_odmapuj(int pid, uint64_t adres, uint64_t dlugosc) {
    if (!pid_poprawny(pid)) return EINVAL;
    if (dlugosc == 0) return EINVAL;
    const uint64_t rozmiar = zaokraglij_w_gore(dlugosc);

    StanMmapProcesu& s = stany[pid];
    for (uint32_t i = 0; i < MMAP_MAKS_REGIONOW; ++i) {
        MmapRegion& r = s.regiony[i];
        if (!r.uzywany) continue;
        if (adres != r.adres) continue;   /* wspieramy tylko od poczatku */
        if (rozmiar == r.dlugosc) {
            for (uint64_t off = 0; off < r.dlugosc; off += STRONA)
                odmapuj_strone(r.adres + off);
            r = {};
            if (s.liczba) --s.liczba;
            return 0;
        }
        if (rozmiar < r.dlugosc) {
            /* Przytnij od poczatku: przesun adres i skroc dlugosc. */
            for (uint64_t off = 0; off < rozmiar; off += STRONA)
                odmapuj_strone(r.adres + off);
            r.adres += rozmiar;
            r.dlugosc -= rozmiar;
            return 0;
        }
        return EINVAL;
    }
    return EINVAL;
}

int64_t mmap_jadro_ochrona(int pid, uint64_t adres, uint64_t dlugosc,
                           uint64_t ochrona) {
    if (!pid_poprawny(pid)) return EINVAL;
    const uint64_t rozmiar = zaokraglij_w_gore(dlugosc);
    if (rozmiar == 0) return EINVAL;

    /* W^X takze przy mprotect - inaczej mmap+W|X bylby obejściem. */
    if ((ochrona & POSIX_PROT_WRITE) && (ochrona & POSIX_PROT_EXEC)) return EACCES;
    if (ochrona & ~(POSIX_PROT_READ | POSIX_PROT_WRITE | POSIX_PROT_EXEC)) return EINVAL;

    StanMmapProcesu& s = stany[pid];
    const int32_t i = mmap_znajdz_region(s.regiony, MMAP_MAKS_REGIONOW, adres, rozmiar);
    if (i < 0) return EINVAL;

    /*
     * Zmiana ochrony wymaga przemapowania stron, bo PTE trzyma RW i NX.
     * VMM nie ma jeszcze API do zmiany flag istniejacego mapowania, wiec
     * zwracamy -ENOSYS i NIE zmieniamy ksiegowania: cicha rozbieznosc
     * miedzy deklaracja a sprzetem jest gorsza niz jawny brak wsparcia.
     */
    (void)s;
    return ENOSYS;
}
