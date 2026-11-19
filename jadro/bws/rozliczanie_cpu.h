#pragma once
/*
 * Rozliczanie czasu procesora per proces (Menedzer zadan, BWS 103).
 *
 * Czysta logika bez stanu globalnego - scheduler trzyma jedna instancje,
 * testy hosta (testy/test_menedzer_zadan_host.cpp) karmia ja kontrolowanym
 * zegarem.
 *
 * Model (jeden procesor):
 *   - Kazdy nanosekundowy odcinek czasu od wlaczenia rozliczania nalezy do
 *     DOKLADNIE jednego kubelka: czas_cpu[pid] albo bezczynnosc.
 *   - Odcinek konczy sie przy kazdym rzeczywistym przelaczeniu kontekstu
 *     (PrzelaczKontekst / programowe oddanie CPU) oraz na granicach HLT
 *     petli PID 0. Czas jest przypisywany procesowi, ktory byl BIEZACY
 *     (aktualny_pid) - lacznie z jego syscallami i z przerwaniami, ktore
 *     przyszly w tym czasie (jadro nie ma osobnego kubelka IRQ).
 *   - PID 0 wykonuje prawdziwa prace jadra (USB, wejscie, kompozytor), wiec
 *     jego czas NIE jest bezczynnoscia. Bezczynnosc to wylacznie czas w HLT
 *     petli PID 0 (miedzy bezczynnosc_poczatek i najblizszym zdarzeniem).
 *     Przerwanie budzace HLT jest liczone do bezczynnosci az do konca
 *     obslugi (kilka mikrosekund) - granica dokumentowana, nie ukrywana.
 *   - Liczniki sa monotoniczne i nigdy nie sa zerowane podczas probkowania.
 *     Nowy proces w slocie dostaje nowa generacje i czas_cpu = 0; konsument
 *     rozpoznaje to po generacji (ponowne uzycie PID), nie po spadku licznika.
 *
 * Zrodla czasu:
 *   ZRODLO_HPET  - monotoniczny zegar ns (dokladne odcinki).
 *   ZRODLO_TICKI - brak HPET: kazdy tick timera (okres_ticku_ns) jest
 *                  przypisywany kubelkowi biezacemu w chwili ticku
 *                  (probkowanie statystyczne, rozdzielczosc 1 tick).
 *   ZRODLO_BRAK  - rozliczanie niedostepne (aplikacja pokazuje "Niedostepne").
 */
#include <stdint.h>

namespace RozliczanieCpu {

inline constexpr int MAKS = 16;

enum Zrodlo : uint32_t { ZRODLO_BRAK = 0, ZRODLO_HPET = 1, ZRODLO_TICKI = 2 };

struct Migawka {
    uint64_t teraz_ns;              // chwila migawki (zegar rozliczania)
    uint64_t rozliczony_ns;         // suma wszystkich kubelkow (= czas od startu rozliczania)
    uint64_t bezczynnosc_ns;
    uint64_t cpu_ns[MAKS];
};

struct Stan {
    Zrodlo zrodlo = ZRODLO_BRAK;
    uint64_t okres_ticku_ns = 0;
    uint64_t start_odcinka = 0;     // poczatek biezacego odcinka (tryb HPET)
    int pid = 0;                    // wlasciciel biezacego odcinka
    bool w_hlt = false;             // biezacy odcinek to bezczynnosc PID 0
    uint64_t bezczynnosc_ns = 0;
    uint64_t rozliczony_ns = 0;
    uint64_t cpu_ns[MAKS] = {};

    static bool pid_ok(int p) { return p >= 0 && p < MAKS; }

    void wlacz(Zrodlo z, uint64_t teraz, int biezacy, uint64_t okres_ticku = 0) {
        zrodlo = z;
        okres_ticku_ns = okres_ticku;
        start_odcinka = teraz;
        pid = pid_ok(biezacy) ? biezacy : 0;
        w_hlt = false;
    }

    // Zegar nie moze sie cofnac; gdyby jednak (blad zrodla), odcinek ma dlugosc 0.
    void ksieguj(uint64_t teraz) {
        if (zrodlo != ZRODLO_HPET) return;
        const uint64_t d = teraz > start_odcinka ? teraz - start_odcinka : 0;
        start_odcinka = teraz > start_odcinka ? teraz : start_odcinka;
        dodaj(d);
    }

    void dodaj(uint64_t d) {
        if (w_hlt) bezczynnosc_ns += d;
        else if (pid_ok(pid)) cpu_ns[pid] += d;
        rozliczony_ns += d;
    }

    // Rzeczywiste przelaczenie: odcinek dotychczasowego wlasciciela sie konczy.
    void przelacz(uint64_t teraz, int nowy_pid) {
        ksieguj(teraz);
        w_hlt = false;
        pid = pid_ok(nowy_pid) ? nowy_pid : 0;
    }

    // Petla PID 0 (z wylaczonymi przerwaniami tuz przed "sti; hlt").
    void bezczynnosc_poczatek(uint64_t teraz) {
        ksieguj(teraz);
        if (pid == 0) w_hlt = true;
    }
    void bezczynnosc_koniec(uint64_t teraz) {
        ksieguj(teraz);
        w_hlt = false;
    }

    // Tryb ZRODLO_TICKI: tick przypisuje swoj okres kubelkowi biezacemu.
    void tick() {
        if (zrodlo == ZRODLO_TICKI) dodaj(okres_ticku_ns);
    }

    // Nowy proces w slocie: licznik startuje od zera (konsument widzi nowa generacje).
    void nowy_proces(int p) {
        if (pid_ok(p)) cpu_ns[p] = 0;
    }

    // Odczyt bez modyfikacji stanu: biezacy odcinek jest doliczany wirtualnie.
    Migawka migawka(uint64_t teraz) const {
        Migawka m{};
        m.teraz_ns = teraz;
        m.bezczynnosc_ns = bezczynnosc_ns;
        m.rozliczony_ns = rozliczony_ns;
        for (int i = 0; i < MAKS; ++i) m.cpu_ns[i] = cpu_ns[i];
        if (zrodlo == ZRODLO_HPET && teraz > start_odcinka) {
            const uint64_t d = teraz - start_odcinka;
            if (w_hlt) m.bezczynnosc_ns += d;
            else if (pid_ok(pid)) m.cpu_ns[pid] += d;
            m.rozliczony_ns += d;
        }
        return m;
    }
};

}  // namespace RozliczanieCpu
