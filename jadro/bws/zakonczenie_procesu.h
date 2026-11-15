#pragma once
/*
 * Cykl zakonczenia procesu: decyzje (czysta logika, testy hosta: testy/test_zakonczenie_procesu_host.cpp).
 *
 * Stany (scheduler.h): PUSTY(0) GOTOWY(1) ZABLOKOWANY(2: rezerwacja loadera) ZABL_MYSZ(3) ZABL_ZDARZENIE(4)
 * ZABL_KLAWIATURA(5) KONCZACY(6).
 *   zywy --zazadaj--> KONCZACY (zasoby zapisane w zasoby_do_zwolnienia, oczekuje=true)
 *   KONCZACY --tick: posprzataj--> PUSTY (zasoby zwolnione RAZ, oczekuje=false)
 * Samozakonczenie (BWS 32) robi to samo dla procesu biezacego. Zadanie wobec KONCZACY jest idempotentne;
 * wobec PUSTY/rezerwacji loadera/PID 0/siebie - odmowa. Destrukcja zawsze w kontekscie timera, nigdy w syscallu.
 *
 * Wymagania na przyszle SMP: (1) zasoby_do_zwolnienia i stan pod ta sama blokada co tablica procesow;
 * (2) przed niszczeniem przestrzeni adresowej - shootdown TLB i potwierdzenie, ze zaden CPU nie ma jej w CR3;
 * (3) zadanie wobec procesu dzialajacego na INNYM CPU wymaga IPI i oczekiwania na opuszczenie Ring 3;
 * (4) wlasciciele blokad jadra musza byc sledzeni (obecnie brak wywlaszczania Ring 0 to gwarantuje).
 */
#include <stdint.h>

namespace ZakonczenieProcesu {

inline constexpr int STAN_PUSTY = 0;
inline constexpr int STAN_ZABLOKOWANY = 2;     // rezerwacja slotu przez loader (budowa procesu)
inline constexpr int STAN_KONCZACY = 6;
inline constexpr int STAN_MAKS = 6;

enum class Decyzja : uint8_t {
    Zakoncz,          // przejdz w KONCZACY i zapisz zasoby
    JuzWTrakcie,      // KONCZACY: nic do zrobienia (idempotentnie), a gdy zasoby nie zapisane - dopisz je
    BrakProcesu,      // PUSTY albo PID spoza zakresu
    Zajety,           // rezerwacja loadera (proces w budowie)
    ProcesBiezacy,    // uzyj samozakonczenia
    ProcesJadra       // PID 0
};

inline Decyzja ocen_zadanie(int pid, int aktualny_pid, int stan, int maks_procesow) {
    if (pid < 0 || pid >= maks_procesow || stan < STAN_PUSTY || stan > STAN_MAKS) return Decyzja::BrakProcesu;
    if (pid == 0) return Decyzja::ProcesJadra;
    if (pid == aktualny_pid) return Decyzja::ProcesBiezacy;
    if (stan == STAN_PUSTY) return Decyzja::BrakProcesu;
    if (stan == STAN_ZABLOKOWANY) return Decyzja::Zajety;
    if (stan == STAN_KONCZACY) return Decyzja::JuzWTrakcie;
    return Decyzja::Zakoncz;
}

// Czy mozna teraz zwolnic zasoby procesu `pid`? (nie bieglo w nim CPU i nie jego CR3 jest aktywny)
inline bool czy_mozna_sprzatac(int pid, int pid_biezacy, bool oczekuje, bool cr3_aktywny) {
    return oczekuje && pid > 0 && pid != pid_biezacy && !cr3_aktywny;
}

// Autoryzacja zadania BWS 97 (pomocnicza regula): zabijajacy ma prawo zarzadzania procesami (PZB <= USLUGI i
// SYSTEM_CONFIG, sprawdzane przez wywolujacego), a cel nie moze byc bardziej zaufany (numerycznie nizszy poziom) ani
// byc samym zabijajacym, ani oknem menedzera (zakonczenie pulpitu zostawiloby system bez GUI).
inline bool czy_wolno_zabic(int pid_zabijajacego, int poziom_zabijajacego, int pid_celu, int poziom_celu, bool cel_to_menedzer_okien) {
    if (pid_celu <= 0 || pid_celu == pid_zabijajacego) return false;
    if (poziom_celu < poziom_zabijajacego) return false;
    if (cel_to_menedzer_okien) return false;
    return true;
}

} // namespace ZakonczenieProcesu
