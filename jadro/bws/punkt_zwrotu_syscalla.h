#pragma once
/*
 * Punkt zwrotu na wejsciu syscallu: czysta logika decyzji (testy hosta: testy/test_punkt_zwrotu_syscalla_host.cpp).
 * Mechanizm opisuje docs/zakonczenie-procesow.md (sekcja "Odroczone tiki i punkt zwrotu syscallu").
 *
 * Proces oddaje CPU programowo (int $IDT_WEKTOR_ODDANIA_CPU) tylko, gdy:
 *   1. od poprzedniego sprawdzenia tick timera trafil w Ring 0 i NIE wykonal PrzelaczKontekst (tick odroczony),
 *   2. biezacy proces jest procesem uzytkownika (pid 1..maks-1), a wielozadaniowosc jest aktywna,
 *   3. przerwania sa wlaczone (IF=1; nigdy z sekcji z wylaczonymi przerwaniami),
 *   4. jakis INNY proces czeka na prace wykonywana tylko przez PrzelaczKontekst.
 *
 * PID 0 jest pelnoprawnym konkurentem (warunek 4): to nie jest pusty idle - jego petla (kernel.cpp) obsluguje USB,
 * wejscie (input_service_all, virtio_input_obsluz), kompozytor (skladacz_obrazu_obsluz_dirty), timeout ekranu,
 * odroczona prace HDA i zmiane geometrii. Pominiecie go zaglodziloby mysz i kompozycje przy ciaglej serii syscalli -
 * dokladnie ten blad, ktory naprawia odroczony tick. PID 0 dostaje wiec CPU z ta sama czestoscia co przy ticku w Ring 3
 * (round-robin w znajdz_nastepny_proces), a sam z HLT wraca najpozniej na nastepnym ticku.
 */
#include <stdint.h>

namespace PunktZwrotuSyscalla {

/* Wartosci PROCES_* z scheduler.h (zgodnosc sprawdza static_assert w scheduler.cpp). */
inline constexpr int STAN_ZABLOKOWANY_MYSZ = 3;
inline constexpr int STAN_ZABLOKOWANY_ZDARZENIE = 4;

/* Migawka jednego slotu procesu, potrzebna do decyzji. */
struct Kandydat {
    bool sprzatanie_oczekuje = false;   // zasoby_do_zwolnienia[pid].oczekuje (dla PID 0 ignorowane)
    bool moze_byc_uruchomiony = false;  // proces_moze_byc_uruchomiony(pid)
    int stan = 0;                       // odczytaj_stan_procesu
    uint64_t termin_myszy = 0;          // termin_wybudzenia_myszy[pid]
    uint64_t termin_zdarzenia = 0;      // termin_wybudzenia_zdarzenia[pid]; 0 = bez timeoutu
};

/* Porownanie odporne na zawiniecie uint64_t (identyczne z tick_osiagnal_termin w scheduler.cpp). */
constexpr bool tick_osiagnal_termin(uint64_t teraz, uint64_t termin) {
    return static_cast<int64_t>(teraz - termin) >= 0;
}

/* Czy proces pid (rozny od biezacego) potrzebuje obslugi, ktora wykonuje tylko PrzelaczKontekst? */
constexpr bool kandydat_wymaga_ticku(int pid, const Kandydat& k, uint64_t tick) {
    if (pid > 0 && k.sprzatanie_oczekuje) return true;                                    // odroczone sprzatanie
    if (k.moze_byc_uruchomiony) return true;                                              // inny gotowy (takze PID 0)
    if (k.stan == STAN_ZABLOKOWANY_MYSZ && tick_osiagnal_termin(tick, k.termin_myszy)) return true;
    if (k.stan == STAN_ZABLOKOWANY_ZDARZENIE && k.termin_zdarzenia != 0 &&
        tick_osiagnal_termin(tick, k.termin_zdarzenia)) return true;                     // wygasly timeout zdarzenia
    return false;
}

/*
 * opis(pid) -> Kandydat; okna() -> czy osierocone okno podrzedne czeka na termin zamkniecia.
 * okna() jest wolane leniwie, tylko gdy zaden proces nie wymaga obslugi.
 */
template <class Opis, class Okna>
bool praca_oczekuje(int biezacy, int maks, uint64_t tick, Opis&& opis, Okna&& okna) {
    for (int pid = 0; pid < maks; ++pid) {
        if (pid == biezacy) continue;
        if (kandydat_wymaga_ticku(pid, opis(pid), tick)) return true;
    }
    return okna();
}

enum class Decyzja : uint8_t {
    BrakOdroczonegoTicku,   // szybka sciezka: nic sie nie wydarzylo
    PozaProcesem,           // PID 0 / niepoprawny PID / przed wlaczeniem wielozadaniowosci
    PrzerwaniaWylaczone,    // IF=0: nigdy nie oddajemy CPU
    BrakPracy,              // nikt nie czeka - proces kontynuuje syscall bez przelaczenia
    Oddaj,                  // int $IDT_WEKTOR_ODDANIA_CPU
};

/* praca() jest wolana tylko, gdy warunki 1-3 sa spelnione. */
template <class Praca>
Decyzja ocen(bool tick_odroczony, int pid, int maks, bool wielozadaniowosc, bool if_wlaczone, Praca&& praca) {
    if (!tick_odroczony) return Decyzja::BrakOdroczonegoTicku;
    if (pid <= 0 || pid >= maks || !wielozadaniowosc) return Decyzja::PozaProcesem;
    if (!if_wlaczone) return Decyzja::PrzerwaniaWylaczone;
    return praca() ? Decyzja::Oddaj : Decyzja::BrakPracy;
}

}  // namespace PunktZwrotuSyscalla
