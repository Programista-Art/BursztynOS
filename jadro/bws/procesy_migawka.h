#pragma once
/*
 * Bursztyn OS - ABI Menedzera zadan: migawka procesow (BWS 103) i zakonczenie
 * procesu wskazanego tozsamoscia (BWS 104). Opis: docs/menedzer-zadan.md.
 *
 * Wymagane prawo: PRAWO_ZARZADZANIE_PROCESAMI i PZB <= PZB_ZAUFANE. Prawo nie
 * jest nadawane przez manifest - wylacznie z tabeli aplikacji systemowych
 * jadra (uruchamianie_polityka.h) dla pliku o wbudowanej sumie SHA-256
 * (Menedzer zadan). Zwykla aplikacja Ring 3 nie moze go uzyskac.
 *
 * BWS 103  arg1 = bufor Ring 3, arg2 = rozmiar bufora w bajtach,
 *          arg3 = wersja ABI (BWS_PROCESY_V2_ABI)
 *   Bufor: BwsProcesyNaglowek, za nim liczba_wpisow * BwsProcesInfoV2.
 *   Zwraca BWS_MIGAWKA_OK albo kod bledu (nic nie jest zapisywane). Gdy bufor
 *   miesci mniej wpisow niz procesow, wpisy sa obciete (rosnacy PID), flaga
 *   BWS_MIGAWKA_OBCIETA jest ustawiona, a liczba_procesow podaje ile ich jest.
 *
 * BWS 104  arg1 = PID, arg2 = generacja (z migawki)
 *   Zwraca BWS_ZAKONCZ_*. Generacja jest sprawdzana atomowo z decyzja (bez
 *   przerwan, jeden procesor), wiec ponownie uzyty PID nigdy nie zostanie
 *   zakonczony zamiast procesu wybranego przez uzytkownika.
 *
 * Zadnych wskaznikow jadra, zadnej pamieci innych procesow - tylko liczniki.
 */
#include <stdint.h>
#include <stddef.h>

inline constexpr uint32_t BWS_PROCESY_V2_ABI = 2U;
inline constexpr uint32_t BWS_PROCESY_V2_MAKS = 16U;
inline constexpr uint32_t BWS_PROCES_SCIEZKA_MAKS = 64U;

/* Flagi naglowka */
inline constexpr uint32_t BWS_MIGAWKA_OBCIETA = 1U << 0;
inline constexpr uint32_t BWS_MIGAWKA_CPU_HPET = 1U << 1;   // dokladne odcinki ns
inline constexpr uint32_t BWS_MIGAWKA_CPU_TICKI = 1U << 2;  // probkowanie tickiem (bez HPET)
inline constexpr uint32_t BWS_MIGAWKA_RAM = 1U << 3;        // liczniki PMM dostepne

/* Flagi wpisu */
inline constexpr uint32_t BWS_PROC_WYWOLUJACY = 1U << 0;     // wpis opisuje proces pytajacy
inline constexpr uint32_t BWS_PROC_JADRO = 1U << 1;          // PID 0
inline constexpr uint32_t BWS_PROC_MENEDZER_OKIEN = 1U << 2;
inline constexpr uint32_t BWS_PROC_PAMIEC_ZMIERZONA = 1U << 3;  // przejscie tablic stron sie udalo
inline constexpr uint32_t BWS_PROC_PAMIEC_NIEPELNA = 1U << 4;   // duze strony / tablica spoza mapy
inline constexpr uint32_t BWS_PROC_MOZNA_ZAKONCZYC = 1U << 5;   // ta sama decyzja co BWS 104

/* Powod ochrony (wpis.ochrona); BRAK = mozna zakonczyc. */
enum BwsOchronaProcesu : uint32_t {
    BWS_OCHRONA_BRAK = 0,
    BWS_OCHRONA_JADRO = 1,            // PID 0: watek jadra
    BWS_OCHRONA_MENEDZER_OKIEN = 2,   // pulpit; zakonczenie zostawiloby system bez GUI
    BWS_OCHRONA_USLUGA = 3,           // PZB < ZAUFANE: jadro, sterowniki, uslugi systemowe
    BWS_OCHRONA_WYZSZE_ZAUFANIE = 4,  // cel bardziej zaufany niz wywolujacy
    BWS_OCHRONA_WLASNY = 5,           // wlasny proces: uzyj zwyklego zamkniecia okna
    BWS_OCHRONA_URUCHAMIANY = 6,      // slot w budowie przez loader
    BWS_OCHRONA_KONCZACY = 7,         // juz konczony (zadanie bylo przyjete)
};

/* Wynik BWS 103 */
enum BwsMigawkaWynik : uint64_t {
    BWS_MIGAWKA_BLAD = 0,
    BWS_MIGAWKA_OK = 1,
    BWS_MIGAWKA_BRAK_UPRAWNIEN = 2,
    BWS_MIGAWKA_ZLY_BUFOR = 3,       // NULL, zly zakres Ring 3, rozmiar < naglowka, przepelnienie
    BWS_MIGAWKA_ZLA_WERSJA = 4,
};

/* Wynik BWS 104 */
enum BwsZakonczWynik : uint64_t {
    BWS_ZAKONCZ_BLAD = 0,
    BWS_ZAKONCZ_ZAAKCEPTOWANO = 1,   // proces przeszedl w KONCZACY; zasoby zwolni scheduler (odroczenie)
    BWS_ZAKONCZ_JUZ_KONCZONY = 2,
    BWS_ZAKONCZ_BRAK_PROCESU = 3,    // slot pusty albo inna generacja (proces sie zakonczyl / PID uzyty ponownie)
    BWS_ZAKONCZ_BRAK_UPRAWNIEN = 4,
    BWS_ZAKONCZ_CHRONIONY = 5,       // proces chroniony (patrz BwsOchronaProcesu w migawce)
    BWS_ZAKONCZ_WLASNY = 6,
    BWS_ZAKONCZ_URUCHAMIANY = 7,
    BWS_ZAKONCZ_ZLE_ARGUMENTY = 8,
};

struct BwsProcesyNaglowek {
    uint32_t wersja;               // BWS_PROCESY_V2_ABI
    uint32_t rozmiar_naglowka;     // sizeof(BwsProcesyNaglowek)
    uint32_t rozmiar_wpisu;        // sizeof(BwsProcesInfoV2)
    uint32_t liczba_wpisow;        // zapisane wpisy
    uint32_t liczba_procesow;      // wszystkie niepuste sloty w chwili migawki
    uint32_t flagi;                // BWS_MIGAWKA_*
    uint64_t czas_ns;              // zegar rozliczania w chwili migawki
    uint64_t czas_rozliczony_ns;   // suma kubelkow CPU + bezczynnosci od startu rozliczania
    uint64_t czas_bezczynnosci_ns; // HLT petli PID 0
    uint64_t okres_ticku_ns;       // rozdzielczosc w trybie tickow (0 przy HPET)
    uint64_t ram_zarzadzana_bajtow;  // ramki PMM (RAM dostepny dla systemu)
    uint64_t ram_wolna_bajtow;
    uint64_t ram_firmware_bajtow;    // RAM zgloszony przez firmware (MEMORY_AVAILABLE)
    uint32_t pid_wywolujacego;
    uint32_t zarezerwowane;
    uint64_t generacja_wywolujacego;
};

struct BwsProcesInfoV2 {
    uint32_t pid;
    uint32_t stan;                 // PROCES_* ze scheduler.h
    uint64_t generacja;            // 0 tylko dla PID 0
    uint32_t poziom_pzb;
    uint32_t flagi;                // BWS_PROC_*
    uint32_t ochrona;              // BwsOchronaProcesu
    uint32_t zarezerwowane;
    uint64_t uprawnienia;          // maska PRAWO_*
    uint64_t czas_cpu_ns;          // skumulowany czas CPU (monotoniczny w obrebie generacji)
    uint64_t czas_startu_ns;       // zegar rozliczania przy rezerwacji slotu
    uint64_t pamiec_prywatna_bajtow;     // ramki USER bez SHARED (zwalniane przy zakonczeniu)
    uint64_t pamiec_tablic_stron_bajtow; // PML4 + tablice prywatnej polowy
    uint64_t pamiec_wspoldzielona_bajtow;// zmapowane ramki SHM (nie wliczane do procesu)
    uint64_t stos_jadra_bajtow;          // prywatny stos Ring 0
    uint64_t bufor_okna_bajtow;          // bufor warstwy kompozytora (pamiec jadra procesu)
    uint64_t sterta_wirtualna_bajtow;    // zarezerwowany zakres sterty (wirtualny, nie RAM)
    char sciezka[BWS_PROCES_SCIEZKA_MAKS]; // sciezka obrazu programu, NUL
};

static_assert(sizeof(BwsProcesyNaglowek) == 96, "ABI BWS 103: naglowek");
static_assert(sizeof(BwsProcesInfoV2) == 168, "ABI BWS 103: wpis");
static_assert(offsetof(BwsProcesInfoV2, sciezka) == 104, "ABI BWS 103: sciezka");

namespace ProcesyMigawka {

/* Ile wpisow miesci bufor; false = bufor nie miesci nawet naglowka (bez przepelnien). */
inline bool pojemnosc_bufora(uint64_t rozmiar, uint32_t* wpisy) {
    if (!wpisy || rozmiar < sizeof(BwsProcesyNaglowek)) return false;
    const uint64_t reszta = rozmiar - sizeof(BwsProcesyNaglowek);
    uint64_t n = reszta / sizeof(BwsProcesInfoV2);
    if (n > BWS_PROCESY_V2_MAKS) n = BWS_PROCESY_V2_MAKS;
    *wpisy = static_cast<uint32_t>(n);
    return true;
}

/* Liczba bajtow do skopiowania; n <= MAKS, wiec iloczyn nie moze sie przepelnic. */
inline uint64_t bajtow_odpowiedzi(uint32_t n) {
    if (n > BWS_PROCESY_V2_MAKS) n = BWS_PROCESY_V2_MAKS;
    return sizeof(BwsProcesyNaglowek) + uint64_t(n) * sizeof(BwsProcesInfoV2);
}

/* Kto moze pytac i konczyc: dedykowane prawo + co najwyzej poziom aplikacji zaufanych. */
inline bool wywolujacy_uprawniony(uint64_t prawa, uint64_t prawo_zarzadzania, uint8_t poziom, uint8_t poziom_zaufane) {
    return (prawa & prawo_zarzadzania) == prawo_zarzadzania && prawo_zarzadzania != 0 && poziom <= poziom_zaufane;
}

struct Cel {
    int pid;
    int stan;                 // PROCES_*
    uint8_t poziom;
    bool menedzer_okien;      // sciezka obrazu = Menedzer Okien (proces_t.sciezka_pliku z loadera)
};

inline constexpr int STAN_PUSTY = 0, STAN_ZABLOKOWANY = 2, STAN_KONCZACY = 6, STAN_MAKS = 6;

/*
 * Jedna regula dla flagi "mozna zakonczyc" w migawce i dla BWS 104.
 * Kolejnosc: istnienie, jadro, wlasny, menedzer okien, uslugi, zaufanie, stan.
 */
inline BwsOchronaProcesu ochrona(int pid_wywolujacego, uint8_t poziom_wywolujacego, const Cel& c,
                                 uint8_t poziom_zaufane) {
    if (c.pid == 0) return BWS_OCHRONA_JADRO;
    if (c.pid == pid_wywolujacego) return BWS_OCHRONA_WLASNY;
    if (c.menedzer_okien) return BWS_OCHRONA_MENEDZER_OKIEN;
    if (c.stan == STAN_ZABLOKOWANY) return BWS_OCHRONA_URUCHAMIANY;
    if (c.stan == STAN_KONCZACY) return BWS_OCHRONA_KONCZACY;
    if (c.poziom < poziom_zaufane) return BWS_OCHRONA_USLUGA;
    if (c.poziom < poziom_wywolujacego) return BWS_OCHRONA_WYZSZE_ZAUFANIE;
    return BWS_OCHRONA_BRAK;
}

/*
 * Pelna decyzja BWS 104 (bez skutkow ubocznych). generacja_slotu to biezaca
 * generacja procesu w slocie pid (0 = brak). Zwraca ZAAKCEPTOWANO, gdy wolno
 * przekazac zadanie schedulerowi.
 */
inline BwsZakonczWynik decyzja_zakonczenia(bool uprawniony, int pid_wywolujacego, uint8_t poziom_wywolujacego,
                                          uint64_t pid64, uint64_t generacja, int maks_procesow,
                                          const Cel& c, uint64_t generacja_slotu, uint8_t poziom_zaufane) {
    if (!uprawniony) return BWS_ZAKONCZ_BRAK_UPRAWNIEN;
    if (pid64 >= static_cast<uint64_t>(maks_procesow) || generacja == 0) return BWS_ZAKONCZ_ZLE_ARGUMENTY;
    if (pid64 == 0) return BWS_ZAKONCZ_CHRONIONY;
    if (c.stan == STAN_PUSTY || c.stan < 0 || c.stan > STAN_MAKS || generacja_slotu != generacja)
        return BWS_ZAKONCZ_BRAK_PROCESU;
    switch (ochrona(pid_wywolujacego, poziom_wywolujacego, c, poziom_zaufane)) {
        case BWS_OCHRONA_BRAK: return BWS_ZAKONCZ_ZAAKCEPTOWANO;
        case BWS_OCHRONA_WLASNY: return BWS_ZAKONCZ_WLASNY;
        case BWS_OCHRONA_URUCHAMIANY: return BWS_ZAKONCZ_URUCHAMIANY;
        case BWS_OCHRONA_KONCZACY: return BWS_ZAKONCZ_JUZ_KONCZONY;
        default: return BWS_ZAKONCZ_CHRONIONY;
    }
}

}  // namespace ProcesyMigawka

/* Surowe dane slotu kopiowane przez scheduler bez przerwan (tylko jadro). */
struct SchedulerSlotMigawki {
    int stan;
    uint8_t poziom;
    uint64_t uprawnienia;
    uint64_t generacja;
    uint64_t czas_startu_ns;
    uint64_t cpu_ns;
    void* przestrzen;            // tylko do pomiaru pamieci w jadrze - nigdy nie kopiowane do Ring 3
    uint64_t granica_sterty;
    char sciezka[BWS_PROCES_SCIEZKA_MAKS];
};
struct SchedulerMigawka {
    uint64_t teraz_ns, rozliczony_ns, bezczynnosc_ns, okres_ticku_ns;
    uint32_t zrodlo;             // RozliczanieCpu::Zrodlo
    int pid_biezacy;
    SchedulerSlotMigawki sloty[BWS_PROCESY_V2_MAKS];
};
