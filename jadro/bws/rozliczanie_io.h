#pragma once
/*
 * Bursztyn OS - liczniki I/O procesow (BWS 106; docs/menedzer-zadan-monitorowanie.md, etap 2).
 *
 * Tozsamosc wlasciciela = (PID, generacja) zapisana W CHWILI ZLECENIA operacji:
 *  - pliki: w wywolaniu plikowym BWS (kontekst procesu wolajacego, operacja synchroniczna),
 *  - siec: przy rezerwacji sesji TCP (tcp_rezerwuj_sesje w syscallu procesu, ktory otwiera polaczenie),
 *    a NIE w chwili nadejscia pakietu - pakiet odebrany w przerwaniu/odpytywaniu trafia do wlasciciela sesji.
 * Licznik trafia do wpisu procesu tylko, gdy generacja wlasciciela == biezaca generacja slotu. Inaczej
 * (proces sie zakonczyl, PID uzyty ponownie) bajty ida do kubelka "zakonczone" - nigdy do nowego procesu.
 * Sesja otwarta przez jadro (PID 0) trafia do kubelka "jadro". Nowa generacja w slocie zaczyna od zera.
 * Zadnych wskaznikow do proces_t: tylko liczby.
 */
#include <stdint.h>
#include <stddef.h>
#include "urzadzenia_telemetria.h"

namespace RozliczanieIo {

enum Rodzaj : uint8_t { PlikOdczyt = 0, PlikZapis = 1, SiecWyslane = 2, SiecOdebrane = 3, LICZBA = 4 };

struct Wpis {
    uint64_t generacja = 0;     // 0 = wpis nieuzywany
    uint64_t v[LICZBA]{};
};

struct Tabela {
    Wpis procesy[BWS_IO_MAKS_PROCESOW];
    uint64_t jadro[LICZBA]{};
    uint64_t zakonczone[LICZBA]{};
};

inline void dodaj_nasycone(uint64_t& c, uint64_t v) { c = c > UINT64_MAX - v ? UINT64_MAX : c + v; }

/* gen_slotu = biezaca generacja slotu pid (scheduler_generacja_procesu); wywolujacy trzyma IF=0. */
inline void dodaj(Tabela& t, int64_t pid, uint64_t gen, uint64_t gen_slotu, Rodzaj r, uint64_t n) {
    if (!n || r >= LICZBA) return;
    if (pid == 0) { dodaj_nasycone(t.jadro[r], n); return; }
    if (pid < 0 || pid >= static_cast<int64_t>(BWS_IO_MAKS_PROCESOW) || gen == 0 || gen != gen_slotu) {
        dodaj_nasycone(t.zakonczone[r], n);
        return;
    }
    Wpis& w = t.procesy[pid];
    if (w.generacja != gen) { w = Wpis{}; w.generacja = gen; }   // nowa generacja: liczniki od zera
    dodaj_nasycone(w.v[r], n);
}

/*
 * Odpowiedz BWS 106: wpis dla kazdego zajetego slotu (gen_slotow[pid] != 0) oraz PID 0 (tylko siec jadra).
 * Wpis, ktorego generacja nie pasuje do slotu, oznacza "brak I/O tej generacji" - zera sa wtedy prawdziwe.
 */
inline void migawka(const Tabela& t, const uint64_t* gen_slotow, uint64_t czas_ns, bool zegar, BwsProcesyIoOdpowiedz& o) {
    o = BwsProcesyIoOdpowiedz{};
    auto& n = o.naglowek;
    n.wersja = BWS_PROCESY_IO_V1_ABI;
    n.rozmiar_naglowka = sizeof(BwsProcesyIoNaglowek);
    n.rozmiar_wpisu = sizeof(BwsProcesIoV1);
    n.flagi = zegar ? BWS_IO_ZEGAR : 0;
    n.czas_ns = zegar ? czas_ns : 0;
    n.siec_jadro_wyslane = t.jadro[SiecWyslane];
    n.siec_jadro_odebrane = t.jadro[SiecOdebrane];
    n.siec_zakonczone_wyslane = t.zakonczone[SiecWyslane];
    n.siec_zakonczone_odebrane = t.zakonczone[SiecOdebrane];
    n.pliki_zakonczone_odczyt = t.zakonczone[PlikOdczyt];
    n.pliki_zakonczone_zapis = t.zakonczone[PlikZapis];
    {
        BwsProcesIoV1& w = o.wpisy[n.liczba_wpisow++];
        w.pid = 0; w.generacja = 0; w.flagi = BWS_IO_SIEC;         // jadro nie uzywa wywolan plikowych BWS
        w.siec_wyslane_bajtow = t.jadro[SiecWyslane];
        w.siec_odebrane_bajtow = t.jadro[SiecOdebrane];
    }
    for (uint32_t pid = 1; pid < BWS_IO_MAKS_PROCESOW && gen_slotow; ++pid) {
        const uint64_t g = gen_slotow[pid];
        if (!g) continue;
        BwsProcesIoV1& w = o.wpisy[n.liczba_wpisow++];
        w.pid = pid; w.generacja = g; w.flagi = BWS_IO_PLIKI | BWS_IO_SIEC;
        const Wpis& z = t.procesy[pid];
        if (z.generacja != g) continue;
        w.pliki_odczyt_bajtow = z.v[PlikOdczyt];
        w.pliki_zapis_bajtow = z.v[PlikZapis];
        w.siec_wyslane_bajtow = z.v[SiecWyslane];
        w.siec_odebrane_bajtow = z.v[SiecOdebrane];
    }
}

/* Wlasciciel sesji (siec.cpp): zapisany przy rezerwacji, kasowany przy zwolnieniu sesji. */
struct Wlasciciel {
    int64_t pid = -1;           // -1 = brak (licznik trafia do "zakonczone")
    uint64_t generacja = 0;
};

inline Tabela g_tabela{};

}  // namespace RozliczanieIo

#if !defined(BURSZTYN_ROZLICZANIE_IO_HOST)
extern "C" uint64_t scheduler_generacja_procesu(int pid);
/* Jadro: dodanie z biezaca generacja slotu, w krotkiej sekcji IF=0 (wolane z syscalli i z odbioru TCP). */
inline void rozliczanie_io_dodaj(int64_t pid, uint64_t gen, RozliczanieIo::Rodzaj r, uint64_t n) {
    if (!n) return;
    uint64_t flagi;
    asm volatile("pushfq\n\tpopq %0\n\tcli" : "=r"(flagi) : : "memory", "cc");
    const uint64_t gen_slotu = pid > 0 ? scheduler_generacja_procesu(static_cast<int>(pid)) : 0;
    RozliczanieIo::dodaj(RozliczanieIo::g_tabela, pid, gen, gen_slotu, r, n);
    if (flagi & (1ULL << 9)) asm volatile("sti" ::: "memory");
}
/* Wywolanie plikowe procesu pid (synchroniczne): wlascicielem jest biezaca generacja. */
inline void rozliczanie_io_pliku(int pid, RozliczanieIo::Rodzaj r, uint64_t n) {
    if (pid <= 0 || !n) return;
    rozliczanie_io_dodaj(pid, scheduler_generacja_procesu(pid), r, n);
}
#endif
