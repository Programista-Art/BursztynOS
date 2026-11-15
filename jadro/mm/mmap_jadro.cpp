/*
 * Bursztyn OS - mmap w jadrze.
 *
 * Mapowanie jest EAGER: kazda strona dostaje ramke od razu, przez
 * ZaalokujRamkeUzytkownika() i VmmMapujNowaStroneProcesu(). Wybor jest swiadomy:
 *
 *   - nie zmieniamy modelu VMM (demand paging wymaga obslugi #PF i liczenia
 *     RSS, co jest osobnym, wiekszym punktem),
 *   - kazda zmapowana strona jest natychmiast sprawdzalna przez
 *     bws_vmm_sprawdz_zakres_uzytkownika(), wiec walidacja BWS dziala bez
 *     zmian,
 *   - file-backed strony sa kopiowane eager z PSF do prywatnych ramek;
 *     MAP_PRIVATE jest poprawnym snapshotem, a MAP_SHARED z zapisem jest
 *     odrzucane (brak page-cache/write-back).
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
#include "uklad_procesu.h"
#include "jadro/fs/psf.h"
#include "jadro/vfs/vfs_jadro.h"

namespace {

constexpr uint64_t STRONA = 4096ULL;
constexpr int64_t EINVAL = -22;
constexpr int64_t ENOMEM = -12;
constexpr int64_t EBADF = -9;
constexpr int64_t ENOSYS = -38;
constexpr int64_t EACCES = -13;
constexpr int64_t EFAULT = -14;

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
bool zmapuj_strone(uint64_t adres, uint64_t ochrona,
                   const char* sciezka = nullptr,
                   uint64_t offset_pliku = 0) {
    void* ramka = ZaalokujRamkeUzytkownika();
    if (!ramka) return false;

    if (sciezka) {
        void* bufor = FizycznyZakresDoWirtualnego(
            reinterpret_cast<uint64_t>(ramka), STRONA);
        if (!bufor) {
            ZwolnijRamke(ramka);
            return false;
        }
        uint32_t odczytano = 0;
        if (!czytaj_fragment_pliku(
                sciezka, offset_pliku, static_cast<char*>(bufor),
                static_cast<uint32_t>(STRONA), &odczytano) ||
            odczytano != STRONA) {
            ZwolnijRamke(ramka);
            return false;
        }
    }

    const bool zapis = (ochrona & POSIX_PROT_WRITE) != 0;
    const bool wykonuj = (ochrona & POSIX_PROT_EXEC) != 0;
    if (!VmmMapujNowaStroneProcesu(reinterpret_cast<void*>(adres), ramka,
                                   zapis, wykonuj)) {
        ZwolnijRamke(ramka);
        return false;
    }
    return true;
}

bool odmapuj_strone(uint64_t adres) {
    void* ramka = VmmOdmapujStroneProcesu(reinterpret_cast<void*>(adres));
    if (!ramka) return false;
    ZwolnijRamke(ramka);
    return true;
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

        blad = blad_flag(flagi, ochrona);
        if (blad) break;

        const bool anonimowe = (flagi & POSIX_MAP_ANONYMOUS) != 0;
        const uint64_t typ = flagi & POSIX_MAP_TYP_MASKA;
        const VfsDeskryptor* plik = nullptr;
        uint64_t rozmiar_pliku_psf = 0;

        if (anonimowe) {
            if (fd != -1 || offset != 0) { blad = EINVAL; break; }
        } else {
            if (fd < 0) { blad = EBADF; break; }
            plik = vfs_jadro_podejrzyj(static_cast<uint32_t>(pid), fd);
            if (!plik || plik->rodzaj != VFS_PLIK ||
                !vfs_moze_odczytac(plik)) {
                blad = EBADF;
                break;
            }
            if ((offset & (STRONA - 1ULL)) != 0) {
                blad = EINVAL;
                break;
            }
            rozmiar_pliku_psf =
                static_cast<uint64_t>(rozmiar_pliku(plik->sciezka));
            /*
             * Ten etap nie obsluguje mapowania za EOF ani zerowania ogona
             * ostatniej strony. Wymagamy, aby caly eager zakres byl
             * rzeczywistymi danymi pliku.
             */
            if (!mmap_zakres_pliku_poprawny(
                    rozmiar_pliku_psf, offset, dlugosc)) {
                blad = EINVAL;
                break;
            }
            /* Brak page-cache/write-back: wspoldzielony zapis bylby pozorem. */
            if (typ == POSIX_MAP_SHARED &&
                (ochrona & POSIX_PROT_WRITE) != 0) {
                blad = ENOSYS;
                break;
            }
        }

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
            if ((adres & (STRONA - 1ULL)) != 0 ||
                !mmap_adres_poprawny(adres, rozmiar)) {
                blad = EINVAL;
                break;
            }
            adres_docelowy = adres;
            const uint64_t koniec = adres_docelowy + rozmiar;
            for (uint32_t i = 0; i < MMAP_MAKS_REGIONOW; ++i) {
                if (!s.regiony[i].uzywany) continue;
                const uint64_t r_koniec =
                    s.regiony[i].adres + s.regiony[i].dlugosc;
                if (adres_docelowy < r_koniec &&
                    s.regiony[i].adres < koniec) {
                    blad = EINVAL;
                    break;
                }
            }
            if (blad) break;
        } else {
            adres_docelowy = mmap_znajdz_miejsce(s.regiony, MMAP_MAKS_REGIONOW, rozmiar);
            if (adres_docelowy == 0) { blad = ENOMEM; break; }
        }

        /* Mapowanie strony po stronie; przy bledzie wycofujemy wszystko. */
        const uint64_t strony = rozmiar / STRONA;
        uint64_t zmapowane = 0;
        bool ok = true;
        for (; zmapowane < strony; ++zmapowane) {
            const uint64_t offset_strony =
                offset + zmapowane * STRONA;
            if (!zmapuj_strone(
                    adres_docelowy + zmapowane * STRONA, ochrona,
                    anonimowe ? nullptr : plik->sciezka,
                    offset_strony)) {
                ok = false;
                break;
            }
        }
        if (!ok) {
            for (uint64_t i = 0; i < zmapowane; ++i)
                (void)odmapuj_strone(adres_docelowy + i * STRONA);
            blad = ENOMEM;
            break;
        }

        s.regiony[indeks].adres = adres_docelowy;
        s.regiony[indeks].dlugosc = rozmiar;
        s.regiony[indeks].ochrona = static_cast<uint32_t>(ochrona);
        s.regiony[indeks].fd = anonimowe ? -1 : fd;
        s.regiony[indeks].offset = anonimowe ? 0 : offset;
        s.regiony[indeks].typ = static_cast<uint32_t>(typ);
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
    if (rozmiar < dlugosc || (adres & (STRONA - 1ULL)) != 0) {
        return EINVAL;
    }

    StanMmapProcesu& s = stany[pid];
    for (uint32_t i = 0; i < MMAP_MAKS_REGIONOW; ++i) {
        MmapRegion& r = s.regiony[i];
        if (!r.uzywany) continue;
        const int32_t region = mmap_znajdz_region(
            s.regiony, MMAP_MAKS_REGIONOW, adres, rozmiar);
        if (region != static_cast<int32_t>(i)) continue;

        const MmapRegion stara = r;
        const uint64_t region_koniec = stara.adres + stara.dlugosc;
        const uint64_t zakres_koniec = adres + rozmiar;
        const uint64_t prefiks = adres - stara.adres;
        const uint64_t sufiks = region_koniec - zakres_koniec;

        uint32_t wolne = 0;
        for (uint32_t j = 0; j < MMAP_MAKS_REGIONOW; ++j) {
            if (!s.regiony[j].uzywany && j != i) ++wolne;
        }
        const uint32_t potrzebne =
            (prefiks != 0 ? 1U : 0U) + (sufiks != 0 ? 1U : 0U);
        if (wolne < potrzebne) return ENOMEM;

        /*
         * Region jest wlasnoscia prywatnych ramek. Najpierw usuwamy PTE i
         * oddajemy kazda ramke do PMM; dopiero potem zmieniamy ksiegowanie.
         */
        for (uint64_t off = 0; off < rozmiar; off += STRONA) {
            if (!VmmCzyPrywatnaStronaProcesu(
                    reinterpret_cast<void*>(adres + off))) {
                return EFAULT;
            }
        }
        for (uint64_t off = 0; off < rozmiar; off += STRONA) {
            if (!odmapuj_strone(adres + off)) return EFAULT;
        }

        uint32_t wolny_indeks = 0;
        if (prefiks != 0) {
            r = stara;
            r.dlugosc = prefiks;
        } else if (sufiks != 0) {
            r = stara;
            r.adres = zakres_koniec;
            r.dlugosc = sufiks;
            r.offset += rozmiar;
        } else {
            r = {};
            if (s.liczba) --s.liczba;
        }

        auto nowy_region = [&](uint64_t nowy_adres, uint64_t nowa_dlugosc,
                               uint64_t przesuniecie) {
            for (; wolny_indeks < MMAP_MAKS_REGIONOW; ++wolny_indeks) {
                if (s.regiony[wolny_indeks].uzywany) continue;
                s.regiony[wolny_indeks] = stara;
                s.regiony[wolny_indeks].adres = nowy_adres;
                s.regiony[wolny_indeks].dlugosc = nowa_dlugosc;
                s.regiony[wolny_indeks].offset += przesuniecie;
                s.regiony[wolny_indeks].uzywany = true;
                ++s.liczba;
                ++wolny_indeks;
                return;
            }
        };

        if (prefiks != 0 && sufiks != 0) {
            nowy_region(zakres_koniec, sufiks, rozmiar);
        } else if (prefiks == 0 && sufiks != 0 && r.uzywany) {
            /* suffix zajal wpis i nie wymaga dodatkowego slotu */
        } else if (prefiks != 0 && sufiks == 0) {
            /* prefix zajal wpis */
        }
        return 0;
    }
    return EINVAL;
}

int64_t mmap_jadro_ochrona(int pid, uint64_t adres, uint64_t dlugosc,
                           uint64_t ochrona) {
    if (!pid_poprawny(pid)) return EINVAL;
    const uint64_t rozmiar = zaokraglij_w_gore(dlugosc);
    if (rozmiar < dlugosc || rozmiar == 0) return EINVAL;

    /* W^X takze przy mprotect - inaczej mmap+W|X bylby obejściem. */
    if ((ochrona & POSIX_PROT_WRITE) && (ochrona & POSIX_PROT_EXEC)) return EACCES;
    if (ochrona & ~(POSIX_PROT_READ | POSIX_PROT_WRITE | POSIX_PROT_EXEC)) return EINVAL;
    /*
     * x86 PTE nie ma osobnego bitu "czytelna": RW/NX potrafi wyrazic
     * tylko R/W/X. Nie publikujemy wiec falszywego PROT_NONE jako
     * mapowania czytelnego.
     */
    if (ochrona == POSIX_PROT_NONE) return ENOSYS;

    StanMmapProcesu& s = stany[pid];
    const int32_t i = mmap_znajdz_region(s.regiony, MMAP_MAKS_REGIONOW, adres, rozmiar);
    if (i < 0) return EINVAL;

    MmapRegion& r = s.regiony[i];
    if (r.typ == POSIX_MAP_SHARED &&
        (ochrona & POSIX_PROT_WRITE) != 0) {
        return ENOSYS;
    }
    const MmapRegion stara = r;
    const uint64_t koniec = adres + rozmiar;
    const uint64_t prefiks = adres - stara.adres;
    const uint64_t sufiks = stara.adres + stara.dlugosc - koniec;
    uint32_t wolne = 0;
    for (uint32_t j = 0; j < MMAP_MAKS_REGIONOW; ++j) {
        if (!s.regiony[j].uzywany && j != static_cast<uint32_t>(i)) ++wolne;
    }
    const uint32_t potrzebne =
        (prefiks != 0 ? 1U : 0U) + (sufiks != 0 ? 1U : 0U);
    if (wolne < potrzebne) return ENOMEM;

    const bool zapis = (ochrona & POSIX_PROT_WRITE) != 0;
    const bool wykonuj = (ochrona & POSIX_PROT_EXEC) != 0;
    if (!VmmZmienOchronaZakresuProcesu(
            reinterpret_cast<void*>(adres), static_cast<size_t>(rozmiar),
            zapis, wykonuj)) {
        return ENOSYS;
    }

    uint32_t wolny_indeks = 0;
    auto dodaj_czesc = [&](uint64_t czesc_adres, uint64_t czesc_dlugosc,
                           uint64_t czesc_offset) {
        for (; wolny_indeks < MMAP_MAKS_REGIONOW; ++wolny_indeks) {
            if (s.regiony[wolny_indeks].uzywany) continue;
            s.regiony[wolny_indeks] = stara;
            s.regiony[wolny_indeks].adres = czesc_adres;
            s.regiony[wolny_indeks].dlugosc = czesc_dlugosc;
            s.regiony[wolny_indeks].offset += czesc_offset;
            s.regiony[wolny_indeks].uzywany = true;
            ++s.liczba;
            ++wolny_indeks;
            return;
        }
    };

    r = stara;
    r.adres = adres;
    r.dlugosc = rozmiar;
    r.ochrona = static_cast<uint32_t>(ochrona);
    r.offset += prefiks;
    if (prefiks != 0) dodaj_czesc(stara.adres, prefiks, 0);
    if (sufiks != 0) dodaj_czesc(koniec, sufiks, prefiks + rozmiar);
    return 0;
}

/*
 * brk(2) w warstwie pamieci: przesuwa granice sterty procesu i mapuje
 * brakujace strony.
 *
 * Kontrakt:
 *   - granica musi byc wyrownana do strony i lezec w oknie sterty,
 *   - rozszerzanie mapuje nowe strony od razu (model eager, jak mmap),
 *   - zmniejszanie tylko przesuwa granice; strony zostaja zmapowane, bo
 *     VMM nie ma API do odmapowania pojedynczej strony w przestrzeni
 *     procesu, a zwolnienie ramki bez usuniecia PTE daje UAF,
 *   - zwraca nowa granice albo 0 przy bledzie (wywolujacy tlumaczy na errno).
 *
 * Ograniczenie "nie zwalniamy stron przy zmniejszeniu" jest jawnie
 * udokumentowane: pamiec pozostaje zmapowana do konca zycia procesu.
 * To bezpieczne (brak UAF), kosztuje RAM i jest lepsze niz cicha korupcja.
 */
uint64_t mmap_jadro_sterta_rozszerz(int pid, uint64_t nowa_granica) {
    if (!pid_poprawny(pid)) return 0;
    if ((nowa_granica & (STRONA - 1ULL)) != 0) return 0;

    const auto& uklad = uklady_procesow[pid];
    const uint64_t dol = uklad.sterta_baza ? uklad.sterta_baza : RING3_HEAP_BASE;
    const uint64_t gora = uklad.sterta_limit
                              ? uklad.sterta_limit
                              : (RING3_HEAP_BASE + 256ULL * 1024ULL * 1024ULL);

    if (nowa_granica < dol || nowa_granica > gora) return 0;

    proces_t& p = tablica_procesow[pid];
    const uint64_t stara = p.granica_sterty;

    if (nowa_granica == stara) return stara;

    if (nowa_granica < stara) {
        /*
         * Zmniejszenie: tylko przesuniecie granicy. Strony zostaja
         * zmapowane i wyzerowane do konca zycia procesu (patrz komentarz
         * nad funkcja). Nowy brk jest widoczny dla kolejnego brk(0).
         */
        p.granica_sterty = nowa_granica;
        return nowa_granica;
    }

    /*
     * Rozszerzenie: mapujemy kazda nowa strone. Przy braku ramki
     * zatrzymujemy sie i zwracamy 0 - granica zostaje na ostatniej
     * poprawnie zmapowanej pozycji, wiec proces nie dostaje "pustej"
     * pamieci, ktora wyglada jak dostepna.
     */
    for (uint64_t va = stara; va < nowa_granica; va += STRONA) {
        if (!zmapuj_strone(va, POSIX_PROT_READ | POSIX_PROT_WRITE)) {
            p.granica_sterty = va;
            return 0;
        }
    }

    p.granica_sterty = nowa_granica;
    return nowa_granica;
}
