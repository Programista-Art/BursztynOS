#pragma once
/*
 * Pamiec fizyczna przypisana procesowi - przejscie po PRYWATNEJ hierarchii
 * tablic stron (PML4[0..255]) dokladnie wedlug tych samych regul, ktorymi
 * zniszcz_hierarchie_procesu() zwalnia pamiec przy zakonczeniu procesu.
 *
 *  prywatna_bajtow    - liscie 4 KiB PRESENT|USER bez PTE_SHARED. Te ramki
 *                       naleza wylacznie do procesu (obraz programu, stos,
 *                       sterta, bufory prywatne) i wracaja do PMM, gdy proces
 *                       sie konczy. Loader Bursztyna nie wspoldzieli ramek
 *                       obrazu miedzy procesami, wiec nic nie jest liczone
 *                       podwojnie.
 *  wspoldzielona_bajtow - liscie z PTE_SHARED (obiekty SHM BWS). Zycie tych
 *                       ramek prowadzi tabela obiektow SHM; ta sama ramka moze
 *                       byc zmapowana w kilku procesach, dlatego NIE jest
 *                       wliczana do pamieci procesu (tylko informacyjnie).
 *  tablice_bajtow     - ramki samych tablic stron prywatnej polowy (PDP/PD/PT),
 *                       zwalniane razem z procesem. Strona PML4 jest liczona
 *                       osobno przez wywolujacego (zawsze 1 ramka).
 *  duze_strony        - liczba lisci 2M/1G z bitem USER: loader ich nie tworzy
 *                       i jadro ich nie zwalnia; ich obecnosc jest zglaszana
 *                       jako niespojnosc, nie zgadywana. Liscie 2M/1G BEZ bitu
 *                       USER to kopia mapowania tozsamosciowego jadra 0..4 GiB
 *                       (boot.S, strony 2 MiB w PML4[0]) - nie sa pamiecia
 *                       procesu i nie oznaczaja pomiaru niepelnego.
 *
 * Wspolna polowa jadra (PML4[256..511]) nie jest odwiedzana.
 */
#include <stdint.h>
#include <stddef.h>

namespace PamiecProcesu {

inline constexpr uint64_t PTE_PRESENT = 1ULL << 0;
inline constexpr uint64_t PTE_USER = 1ULL << 2;
inline constexpr uint64_t PDE_PS = 1ULL << 7;
inline constexpr uint64_t PTE_SHARED = 1ULL << 9;   // bit dostepny dla OS, jak w vmm.cpp
inline constexpr uint64_t STRONA = 4096ULL;

struct Wynik {
    uint64_t prywatna_bajtow = 0;
    uint64_t wspoldzielona_bajtow = 0;
    uint64_t tablice_bajtow = 0;
    uint64_t duze_strony = 0;
    uint64_t pominiete_tablice = 0;   // wpis wskazuje tablice spoza mapy bezposredniej
};

/*
 * Tabela(wpis) -> const uint64_t* (512 wpisow) albo nullptr.
 * WpisJadra(indeks_pml4) -> true dla wspolnego poddrzewa jadra.
 */
template <class Tabela, class WpisJadra>
inline void policz_poziom(const uint64_t* t, int poziom, Tabela&& tabela, WpisJadra&& wpis_jadra, Wynik& w) {
    for (size_t i = 0; i < 512; ++i) {
        if (poziom == 4 && wpis_jadra(i)) continue;
        const uint64_t e = t[i];
        if ((e & PTE_PRESENT) == 0) continue;
        if (poziom == 1) {
            if ((e & PTE_USER) == 0) continue;            // supervisor: mapowanie jadra
            if (e & PTE_SHARED) w.wspoldzielona_bajtow += STRONA;
            else w.prywatna_bajtow += STRONA;
            continue;
        }
        if ((poziom == 3 || poziom == 2) && (e & PDE_PS)) { if (e & PTE_USER) ++w.duze_strony; continue; }
        const uint64_t* dziecko = tabela(e);
        if (!dziecko) { ++w.pominiete_tablice; continue; }
        w.tablice_bajtow += STRONA;
        policz_poziom(dziecko, poziom - 1, tabela, wpis_jadra, w);
    }
}

template <class Tabela, class WpisJadra>
inline Wynik policz(const uint64_t* pml4, Tabela&& tabela, WpisJadra&& wpis_jadra) {
    Wynik w{};
    if (!pml4) return w;
    policz_poziom(pml4, 4, tabela, wpis_jadra, w);
    w.tablice_bajtow += STRONA;                           // sama strona PML4 procesu
    return w;
}

}  // namespace PamiecProcesu
