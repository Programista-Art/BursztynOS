/*
 * Bursztyn OS - scheduler Round-Robin
 *
 * Odpowiada za:
 *  - tablice procesow,
 *  - wybor kolejnego procesu GOTOWEGO,
 *  - zapis/odtworzenie kernel_rsp,
 *  - przelaczanie CR3,
 *  - aktualizacje TSS.rsp0 oraz stosu SYSCALL,
 *  - asynchroniczne oczekiwanie na zdarzenia myszy,
 *  - timeout oczekiwania na mysz,
 *  - wykrywanie uruchomionej instancji programu,
 *  - odroczone zwalnianie zasobow zakonczonych procesow.
 *
 * WAZNE:
 * PrzelaczKontekst() jest wywolywane przez dispatcher IDT dopiero po
 * wyslaniu EOI Local APIC. Kod przerwania nastepnie ustawia RSP na wartosc
 * zwrocona przez scheduler i wykonuje iretq.
 *
 * Scheduler nie moze przelaczac procesu w srodku zwyklego SYSCALL, poniewaz
 * rama SYSCALL ma inny format niz RejestryStanowe z IRQ. Ta polityka jest
 * realizowana w idt.cpp.
 */

#include "scheduler.h"
#include "pzb.h"
#include "pamiec.h"
#include "heap.h"

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* =========================================================================
 * ZGODNOSC KONFIGURACJI
 * ========================================================================= */

static_assert(
    MAKS_PROCESOW == PZB_MAKS_PROCESOW,
    "scheduler.h i pzb.h maja rozne limity procesow"
);

/* =========================================================================
 * GLOBALNY STAN SCHEDULERA
 * ========================================================================= */

proces_t tablica_procesow[MAKS_PROCESOW] = {};

int aktualny_pid = -1;

bool wielozadaniowosc_aktywna = false;

/*
 * Event counter myszy.
 *
 * Wartosci zaczynamy od 1, aby 0 moglo pozostac naturalna wartoscia
 * niezainicjalizowanych snapshotow.
 */
volatile uint64_t globalne_zdarzenia_myszy = 1;

uint64_t ostatnie_zdarzenie_myszy_procesu[
    MAKS_PROCESOW
] = {};

/*
 * Monotoniczny licznik wywolan planisty przez timer.
 * Nie jest to czas w milisekundach - dopoki timer APIC nie jest
 * skalibrowany, jednostka pozostaje "tickiem schedulera".
 */
static uint64_t licznik_tykniec_zegara = 0;
static uint64_t licznik_irq_timera = 0;
static uint64_t licznik_przelaczen_kontekstu = 0;
static uint64_t licznik_wybudzen_event = 0;

/*
 * Stary kod wybudzal wszystkie procesy myszy co 20 tickow.
 * Zachowujemy podobna semantyke jako indywidualny timeout procesu.
 */
static constexpr uint64_t TIMEOUT_MYSZY_TICKI = 20ULL;

static uint64_t termin_wybudzenia_myszy[
    MAKS_PROCESOW
] = {};

namespace {
/* Burst klawiatury nie moze konkurowac z gestem myszy o 31 slotow. Ruch i
 * timer sa koalescowane, a 127 wpisow daje zapas dla zdarzen krawedziowych
 * podczas kosztownego redraw aplikacji Ring 3. */
constexpr uint32_t ROZMIAR_KOLEJKI_ZDARZEN = 128;
struct KolejkaZdarzen {
    bws_zdarzenie wpisy[ROZMIAR_KOLEJKI_ZDARZEN];
    uint32_t glowa;
    uint32_t ogon;
    uint64_t przepelnienia;
};
KolejkaZdarzen kolejki_zdarzen[MAKS_PROCESOW] = {};
}

/* =========================================================================
 * POLACZENIE Z TSS / SYSCALL / VMM
 * ========================================================================= */

extern "C" void ustaw_stos_jadra_tss(
    uint64_t rsp0
);

extern uint64_t bezpieczny_stos_jadra;

/*
 * Hook istnieje w poprawionym vmm.cpp.
 *
 * Weak pozostawia scheduler kompatybilny z wersja VMM bez tego API.
 * Brak hooka oznacza wyciek tablic stron po zakonczeniu procesu, ale nie
 * ryzykujemy zwolnienia pamieci niewlasciwym mechanizmem.
 */
extern "C" void ZniszczPrzestrzenAdresowaProcesu(
    void* pml4
) __attribute__((weak));

/* =========================================================================
 * POMOCNICZE OPERACJE CPU
 * ========================================================================= */

namespace {

struct StanPrzerwan {
    uint64_t rflags;
};

static inline StanPrzerwan zapisz_i_wylacz_przerwania() {
    StanPrzerwan stan{};

    asm volatile(
        "pushfq\n\t"
        "popq %0\n\t"
        "cli"
        : "=r"(stan.rflags)
        :
        : "memory", "cc"
    );

    return stan;
}

static inline void przywroc_przerwania(
    StanPrzerwan stan
) {
    if ((stan.rflags & (1ULL << 9)) != 0) {
        asm volatile(
            "sti"
            :
            :
            : "memory"
        );
    }
}

static inline uint64_t odczytaj_cr3() {
    uint64_t cr3 = 0;

    asm volatile(
        "mov %%cr3, %0"
        : "=r"(cr3)
        :
        : "memory"
    );

    /*
     * Dolne 12 bitow to flagi/PCID, nie adres PML4.
     */
    return cr3 &
           0x000FFFFFFFFFF000ULL;
}

static inline int odczytaj_stan_procesu(
    const proces_t& proces
) {
    return __atomic_load_n(
        &proces.stan,
        __ATOMIC_ACQUIRE
    );
}

static inline void ustaw_stan_procesu(
    proces_t& proces,
    int stan
) {
    __atomic_store_n(
        &proces.stan,
        stan,
        __ATOMIC_RELEASE
    );
}

static inline bool pid_poprawny(
    int pid
) {
    return
        pid >= 0 &&
        pid < MAKS_PROCESOW;
}

static inline bool pid_uzytkownika(
    int pid
) {
    return
        pid > 0 &&
        pid < MAKS_PROCESOW;
}

static bool cr3_poprawne(
    uint64_t cr3
) {
    return
        cr3 != 0 &&
        (cr3 & 0xFFFULL) == 0;
}

static bool stos_jadra_procesu_poprawny(
    const proces_t& proces
) {
    if (proces.szczyt_stosu_jadra == 0) {
        return false;
    }

    if ((proces.szczyt_stosu_jadra &
         0xFULL) != 0) {
        return false;
    }

    /*
     * PID 0 korzysta z bootowego stosu jadra i nie ma koniecznie
     * baza_stosu_jadra zaalokowanej przez heap.
     */
    if (proces.pid == 0) {
        return true;
    }

    if (proces.baza_stosu_jadra == 0 ||
        proces.szczyt_stosu_jadra <=
            proces.baza_stosu_jadra) {

        return false;
    }

    if (proces.kernel_rsp <
            proces.baza_stosu_jadra ||
        proces.kernel_rsp >=
            proces.szczyt_stosu_jadra) {

        return false;
    }

    return true;
}

static bool proces_moze_byc_uruchomiony(
    int pid
) {
    if (!pid_poprawny(pid)) {
        return false;
    }

    const proces_t& proces =
        tablica_procesow[pid];

    if (odczytaj_stan_procesu(proces) !=
        PROCES_GOTOWY) {

        return false;
    }

    if (proces.kernel_rsp == 0) {
        return false;
    }

    if (!cr3_poprawne(
            proces.cr3)) {

        return false;
    }

    if (!stos_jadra_procesu_poprawny(
            proces)) {

        return false;
    }

    if (pid_uzytkownika(pid)) {
        if (!proces.przestrzen_adresowa) {
            return false;
        }

        if (proces.poziom_zaufania >
            PZB_MAX_POZIOM) {

            return false;
        }
    }

    return true;
}

/* =========================================================================
 * SCIEZKI PROCESOW
 * ========================================================================= */

static bool sciezki_rowne(
    const char* a,
    const char* b
) {
    if (!a || !b) {
        return false;
    }

    for (size_t i = 0;
         i <
            static_cast<size_t>(
                PZB_DLUGOSC_SCIEZKI_PROCESU);
         ++i) {

        const char ca = a[i];
        const char cb = b[i];

        if (ca != cb) {
            return false;
        }

        if (ca == '\0') {
            return true;
        }
    }

    /*
     * Brak NUL w buforze procesu oznacza uszkodzony deskryptor.
     */
    return false;
}

static bool sciezka_wejsciowa_poprawna(
    const char* sciezka
) {
    if (!sciezka) {
        return false;
    }

    for (size_t i = 0;
         i <
            static_cast<size_t>(
                PZB_DLUGOSC_SCIEZKI_PROCESU);
         ++i) {

        if (sciezka[i] == '\0') {
            return i != 0;
        }
    }

    /*
     * Nie obcinamy dlugiej sciezki po cichu.
     */
    return false;
}

/* =========================================================================
 * TIMEOUTY MYSZY
 * ========================================================================= */

static bool tick_osiagnal_termin(
    uint64_t teraz,
    uint64_t termin
) {
    /*
     * Porownanie odporne na zawiniecie uint64_t, jezeli timeout jest
     * znacznie mniejszy niz 2^63 tickow.
     */
    return
        static_cast<int64_t>(
            teraz - termin) >= 0;
}

static void obsluz_timeouty_myszy() {
    for (int pid = 1;
         pid < MAKS_PROCESOW;
         ++pid) {

        proces_t& proces =
            tablica_procesow[pid];

        if (odczytaj_stan_procesu(proces) !=
            PROCES_ZABLOKOWANY_MYSZ) {

            continue;
        }

        const uint64_t termin =
            __atomic_load_n(
                &termin_wybudzenia_myszy[pid],
                __ATOMIC_RELAXED
            );

        if (!tick_osiagnal_termin(
                licznik_tykniec_zegara,
                termin)) {

            continue;
        }

        ustaw_stan_procesu(
            proces,
            PROCES_GOTOWY
        );
    }
}

/* =========================================================================
 * ODROCZONE SPRZATANIE PROCESOW
 * ========================================================================= */

/*
 * Stosu Ring 0 nie wolno zwolnic w zakoncz_aktualny_proces(), bo funkcja
 * wykonuje sie wlasnie na tym stosie.
 *
 * Analogicznie PML4 aktualnego procesu nie wolno niszczyc, gdy CR3 nadal
 * na nie wskazuje.
 *
 * Dlatego zasoby sa zapisywane tutaj i zwalniane przy pozniejszym ticku,
 * gdy CPU wykonuje juz inny proces.
 */
struct ZasobyDoZwolnienia {
    void* przestrzen_adresowa;
    void* baza_stosu_jadra;
    bool oczekuje;
};

static ZasobyDoZwolnienia zasoby_do_zwolnienia[
    MAKS_PROCESOW
] = {};

static void posprzataj_zakonczone_procesy(
    int pid_biezacy
) {
    const uint64_t biezacy_cr3 =
        odczytaj_cr3();

    for (int pid = 1;
         pid < MAKS_PROCESOW;
         ++pid) {

        if (pid == pid_biezacy) {
            continue;
        }

        ZasobyDoZwolnienia& zasoby =
            zasoby_do_zwolnienia[pid];

        if (!zasoby.oczekuje) {
            continue;
        }

        /*
         * Najpierw PML4 i strony user.
         */
        if (zasoby.przestrzen_adresowa) {
            const uint64_t stare_cr3 =
                reinterpret_cast<uint64_t>(
                    zasoby.przestrzen_adresowa
                ) &
                0x000FFFFFFFFFF000ULL;

            if (stare_cr3 != 0 &&
                stare_cr3 != biezacy_cr3 &&
                ZniszczPrzestrzenAdresowaProcesu) {

                ZniszczPrzestrzenAdresowaProcesu(
                    zasoby.przestrzen_adresowa
                );

                zasoby.przestrzen_adresowa =
                    nullptr;
            }
        }

        /*
         * Prywatny stos Ring 0 pochodzi z kmalloc().
         */
        if (zasoby.baza_stosu_jadra) {
            kfree(
                zasoby.baza_stosu_jadra
            );

            zasoby.baza_stosu_jadra =
                nullptr;
        }

        /*
         * Gdy hook VMM nie istnieje, nie blokujemy ponownego uzycia PID.
         * Pozostanie tylko bezpieczny wyciek strony/tabel VMM.
         */
        zasoby.oczekuje = false;
        proces_t& proces=tablica_procesow[pid];
        proces.sciezka_pliku[0]='\0';
        proces.przestrzen_adresowa=nullptr;
        proces.kernel_rsp=0;
        proces.baza_stosu_jadra=0;
        proces.szczyt_stosu_jadra=0;
        ustaw_stan_procesu(proces,PROCES_PUSTY);
    }
}

/* =========================================================================
 * INICJALIZACJA SLOTU
 * ========================================================================= */

static void wyzeruj_slot_procesu(
    proces_t& proces,
    uint64_t pid
) {
    proces = {};

    proces.pid = pid;
    proces.poziom_zaufania =
        PZB_PIASKOWNICA;

    proces.uprawnienia =
        PRAWA_BRAK;

    proces.przestrzen_adresowa =
        nullptr;

    proces.kernel_rsp = 0;
    proces.baza_stosu_jadra = 0;
    proces.szczyt_stosu_jadra = 0;
    proces.cr3 = 0;
    proces.granica_sterty = 0;
    proces.sciezka_pliku[0] = '\0';

    ustaw_stan_procesu(
        proces,
        PROCES_PUSTY
    );
}

/* =========================================================================
 * WYBOR NASTEPNEGO PID
 * ========================================================================= */

static int znajdz_nastepny_proces(
    int biezacy
) {
    if (!pid_poprawny(biezacy)) {
        /*
         * Przy uszkodzonym biezacym PID probujemy uratowac system przez
         * wejscie w PID 0.
         */
        if (proces_moze_byc_uruchomiony(0)) {
            return 0;
        }

        return -1;
    }

    /*
     * Typowy round-robin: zaczynamy OD procesu po aktualnym.
     */
    for (int krok = 1;
         krok <= MAKS_PROCESOW;
         ++krok) {

        const int kandydat =
            (biezacy + krok) %
            MAKS_PROCESOW;

        if (proces_moze_byc_uruchomiony(
                kandydat)) {

            return kandydat;
        }
    }

    return -1;
}

} // namespace

/* =========================================================================
 * OCZEKIWANIE NA MYSZ
 * ========================================================================= */

void ZablokujAktualnyProcesNaMyszy() {
    const StanPrzerwan stan_irq =
        zapisz_i_wylacz_przerwania();

    const int pid =
        aktualny_pid;

    if (!pid_uzytkownika(pid)) {
        przywroc_przerwania(
            stan_irq
        );
        return;
    }

    proces_t& proces =
        tablica_procesow[pid];

    if (odczytaj_stan_procesu(proces) !=
        PROCES_GOTOWY) {

        przywroc_przerwania(
            stan_irq
        );
        return;
    }

    const uint64_t aktualne_zdarzenie =
        __atomic_load_n(
            &globalne_zdarzenia_myszy,
            __ATOMIC_ACQUIRE
        );

    const uint64_t ostatnio_widziane =
        __atomic_load_n(
            &ostatnie_zdarzenie_myszy_procesu[pid],
            __ATOMIC_RELAXED
        );

    if (ostatnio_widziane !=
        aktualne_zdarzenie) {

        /*
         * Istnieje niezuzyte zdarzenie. Konsumujemy jego aktualny numer
         * i NIE blokujemy procesu.
         */
        __atomic_store_n(
            &ostatnie_zdarzenie_myszy_procesu[pid],
            aktualne_zdarzenie,
            __ATOMIC_RELEASE
        );

        przywroc_przerwania(
            stan_irq
        );
        return;
    }

    /*
     * Nie wykonujemy tutaj HLT.
     *
     * Funkcja moze byc wywolana z BWS/SYSCALL. Zatrzymanie CPU w Ring 0
     * powodowalo, ze dispatcher IDT celowo nie wykonywal przelaczenia
     * kontekstu (rama SYSCALL != rama IRQ), przez co jeden proces mogl
     * zablokowac cala maszyne.
     *
     * Zamiast tego oznaczamy proces jako oczekujacy i wracamy. Po powrocie
     * do Ring 3 najblizszy timer zapisze standardowa rame IRQ i scheduler
     * bezpiecznie wybierze inne zadanie.
     */
    __atomic_store_n(
        &termin_wybudzenia_myszy[pid],
        licznik_tykniec_zegara +
            TIMEOUT_MYSZY_TICKI,
        __ATOMIC_RELAXED
    );

    ustaw_stan_procesu(
        proces,
        PROCES_ZABLOKOWANY_MYSZ
    );

    przywroc_przerwania(
        stan_irq
    );
}

extern "C" void WybudzProcesyOczekujaceNaMysz() {
    /*
     * Funkcja jest zwykle wywolywana z IRQ12, wiec nie bierze blokady
     * schedulera ani nie wykonuje STI.
     */
    const uint64_t nowe_zdarzenie =
        __atomic_add_fetch(
            &globalne_zdarzenia_myszy,
            1ULL,
            __ATOMIC_RELEASE
        );

    /*
     * Teoretyczne zawiniecie 2^64 zdarzen nie zmienia poprawnosci
     * protokolu snapshotowego w praktycznym czasie zycia systemu.
     */
    (void)nowe_zdarzenie;

    for (int pid = 1;
         pid < MAKS_PROCESOW;
         ++pid) {

        proces_t& proces =
            tablica_procesow[pid];

        if (odczytaj_stan_procesu(proces) ==
            PROCES_ZABLOKOWANY_MYSZ) {

            ustaw_stan_procesu(
                proces,
                PROCES_GOTOWY
            );
        }
    }
}

bool scheduler_dodaj_zdarzenie(
    int pid, const bws_zdarzenie* zdarzenie) {
    if (!pid_uzytkownika(pid) || !zdarzenie)
        return false;

    KolejkaZdarzen& q = kolejki_zdarzen[pid];
    const StanPrzerwan irq = zapisz_i_wylacz_przerwania();
    uint32_t glowa = q.glowa;
    const uint32_t ogon = q.ogon;

    /* MOVE zastępuje najnowszy nieodebrany MOVE po ostatniej krawędzi.
       TIMER pomiędzy nimi nie może tworzyć historii pozycji kursora. */
    if ((zdarzenie->typ == BWS_ZDARZENIE_MYSZ_RUCH ||
         zdarzenie->typ == BWS_ZDARZENIE_TIMER) && glowa != ogon) {
        uint32_t szukaj=(glowa+ROZMIAR_KOLEJKI_ZDARZEN-1U)%ROZMIAR_KOLEJKI_ZDARZEN;
        bool znaleziono=false;
        while(true){
            const uint8_t typ=q.wpisy[szukaj].typ;
            if(typ==zdarzenie->typ){znaleziono=true;break;}
            if(typ==BWS_ZDARZENIE_MYSZ_DOWN||typ==BWS_ZDARZENIE_MYSZ_UP||
               typ==BWS_ZDARZENIE_KLAWISZ||typ==BWS_ZDARZENIE_FOCUS||
               typ==BWS_ZDARZENIE_BLUR||typ==BWS_ZDARZENIE_ZAMKNIJ)break;
            if(szukaj==ogon)break;
            szukaj=(szukaj+ROZMIAR_KOLEJKI_ZDARZEN-1U)%ROZMIAR_KOLEJKI_ZDARZEN;
        }
        if (znaleziono) {
            q.wpisy[szukaj] = *zdarzenie;
            if (odczytaj_stan_procesu(tablica_procesow[pid]) ==
                PROCES_ZABLOKOWANY_ZDARZENIE)
                ustaw_stan_procesu(tablica_procesow[pid], PROCES_GOTOWY);
            przywroc_przerwania(irq);
            return true;
        }
    }

    uint32_t nastepna = (glowa + 1U) % ROZMIAR_KOLEJKI_ZDARZEN;
    if (nastepna == ogon) {
        /* Dla DOWN/UP/KEY wyrzucamy stary ruch/timer, nigdy krawedz. */
        uint32_t usun = ogon;
        while (usun != glowa &&
               q.wpisy[usun].typ != BWS_ZDARZENIE_MYSZ_RUCH &&
               q.wpisy[usun].typ != BWS_ZDARZENIE_TIMER)
            usun = (usun + 1U) % ROZMIAR_KOLEJKI_ZDARZEN;
        if (usun == glowa) {
            ++q.przepelnienia;
            przywroc_przerwania(irq);
            return false;
        }
        uint32_t i = usun;
        uint32_t kolejny = (i + 1U) % ROZMIAR_KOLEJKI_ZDARZEN;
        while (kolejny != glowa) {
            q.wpisy[i] = q.wpisy[kolejny];
            i = kolejny;
            kolejny = (kolejny + 1U) % ROZMIAR_KOLEJKI_ZDARZEN;
        }
        glowa = (glowa + ROZMIAR_KOLEJKI_ZDARZEN - 1U) %
                ROZMIAR_KOLEJKI_ZDARZEN;
        q.glowa = glowa;
        nastepna = (glowa + 1U) % ROZMIAR_KOLEJKI_ZDARZEN;
        ++q.przepelnienia;
    }
    q.wpisy[glowa] = *zdarzenie;
    q.glowa = nastepna;
    if (odczytaj_stan_procesu(tablica_procesow[pid]) ==
        PROCES_ZABLOKOWANY_ZDARZENIE) {
        ustaw_stan_procesu(tablica_procesow[pid], PROCES_GOTOWY);
        __atomic_add_fetch(&licznik_wybudzen_event,1ULL,__ATOMIC_RELAXED);
    }
    przywroc_przerwania(irq);
    return true;
}

bool scheduler_pobierz_zdarzenie(
    int pid, bws_zdarzenie* zdarzenie) {
    if (!pid_uzytkownika(pid) || !zdarzenie)
        return false;
    KolejkaZdarzen& q = kolejki_zdarzen[pid];
    const StanPrzerwan irq = zapisz_i_wylacz_przerwania();
    if (q.ogon == q.glowa) {
        przywroc_przerwania(irq);
        return false;
    }
    *zdarzenie = q.wpisy[q.ogon];
    q.ogon = (q.ogon + 1U) % ROZMIAR_KOLEJKI_ZDARZEN;
    przywroc_przerwania(irq);
    return true;
}

void scheduler_czekaj_na_zdarzenie(int pid) {
    if (!pid_uzytkownika(pid)) return;
    const StanPrzerwan irq = zapisz_i_wylacz_przerwania();
    KolejkaZdarzen& q = kolejki_zdarzen[pid];
    if (q.ogon == q.glowa &&
        odczytaj_stan_procesu(tablica_procesow[pid]) == PROCES_GOTOWY)
        ustaw_stan_procesu(tablica_procesow[pid],
                           PROCES_ZABLOKOWANY_ZDARZENIE);
    przywroc_przerwania(irq);
}

void scheduler_usun_zdarzenia_procesu(int pid) {
    if (!pid_uzytkownika(pid)) return;
    const StanPrzerwan irq = zapisz_i_wylacz_przerwania();
    kolejki_zdarzen[pid] = {};
    przywroc_przerwania(irq);
}

void scheduler_czekaj_na_klawiature(int pid) {
    if (!pid_uzytkownika(pid)) return;
    const StanPrzerwan irq=zapisz_i_wylacz_przerwania();
    if(odczytaj_stan_procesu(tablica_procesow[pid])==PROCES_GOTOWY)
        ustaw_stan_procesu(tablica_procesow[pid],PROCES_ZABLOKOWANY_KLAWIATURA);
    przywroc_przerwania(irq);
}

extern "C" char pobierz_znak_klawiatury();
char scheduler_pobierz_klawisz_lub_zablokuj(int pid) {
    if(!pid_uzytkownika(pid))return 0;
    const StanPrzerwan irq=zapisz_i_wylacz_przerwania();
    const char znak=pobierz_znak_klawiatury();
    if(znak==0&&odczytaj_stan_procesu(tablica_procesow[pid])==PROCES_GOTOWY)
        ustaw_stan_procesu(tablica_procesow[pid],PROCES_ZABLOKOWANY_KLAWIATURA);
    przywroc_przerwania(irq);
    return znak;
}

extern "C" void scheduler_wybudz_klawiature() {
    for(int pid=1;pid<MAKS_PROCESOW;++pid)
        if(odczytaj_stan_procesu(tablica_procesow[pid])==PROCES_ZABLOKOWANY_KLAWIATURA)
            ustaw_stan_procesu(tablica_procesow[pid],PROCES_GOTOWY);
}

uint64_t scheduler_pobierz_tick() {
    return __atomic_load_n(&licznik_tykniec_zegara, __ATOMIC_RELAXED);
}

void scheduler_zarejestruj_irq_timera(){__atomic_add_fetch(&licznik_irq_timera,1ULL,__ATOMIC_RELAXED);}
uint64_t scheduler_liczba_irq_timera(){return __atomic_load_n(&licznik_irq_timera,__ATOMIC_RELAXED);}
uint64_t scheduler_liczba_przelaczen(){return __atomic_load_n(&licznik_przelaczen_kontekstu,__ATOMIC_RELAXED);}
uint64_t scheduler_liczba_wybudzen_event(){return __atomic_load_n(&licznik_wybudzen_event,__ATOMIC_RELAXED);}

/* =========================================================================
 * INICJALIZACJA PLANISTY
 * ========================================================================= */

void InicjalizujPlaniste(
    uint64_t kernel_rsp0,
    uint64_t cr3
) {
    const StanPrzerwan stan_irq =
        zapisz_i_wylacz_przerwania();

    wielozadaniowosc_aktywna =
        false;

    aktualny_pid = -1;

    licznik_tykniec_zegara = 0;

    __atomic_store_n(
        &globalne_zdarzenia_myszy,
        1ULL,
        __ATOMIC_RELEASE
    );

    for (int i = 0;
         i < MAKS_PROCESOW;
         ++i) {

        wyzeruj_slot_procesu(
            tablica_procesow[i],
            static_cast<uint64_t>(i)
        );

        ostatnie_zdarzenie_myszy_procesu[i] = 0;
        termin_wybudzenia_myszy[i] = 0;
        kolejki_zdarzen[i] = {};

        zasoby_do_zwolnienia[i] = {};
    }

    /*
     * Fail closed przy ewidentnie uszkodzonych parametrach.
     *
     * kernel_rsp0 jest szczytem bootowego stosu i musi byc wyrownany do 16.
     */
    const uint64_t czysty_cr3 =
        cr3 &
        0x000FFFFFFFFFF000ULL;

    if (kernel_rsp0 == 0 ||
        (kernel_rsp0 & 0xFULL) != 0 ||
        !cr3_poprawne(czysty_cr3)) {

        przywroc_przerwania(
            stan_irq
        );
        return;
    }

    proces_t& jadro =
        tablica_procesow[0];

    jadro.pid = 0;
    jadro.poziom_zaufania =
        PZB_JADRO;

    /*
     * Jadro otrzymuje wszystkie prawa ZNANE obecnej wersji PZB zamiast
     * UINT64_MAX. Nieznane bity nie powinny byc automatycznie traktowane
     * jako przyszle uprawnienia.
     */
    jadro.uprawnienia =
        PRAWA_ZNANE;

    jadro.przestrzen_adresowa =
        reinterpret_cast<void*>(
            czysty_cr3
        );

    /*
     * kernel_rsp zostanie uzupelniony przez pierwsze przerwanie timera.
     * Sam kernel_rsp0 jest szczytem stosu, a nie gotowa rama IRQ.
     */
    jadro.kernel_rsp = 0;

    jadro.baza_stosu_jadra = 0;
    jadro.szczyt_stosu_jadra =
        kernel_rsp0;

    jadro.cr3 =
        czysty_cr3;

    jadro.granica_sterty = 0;

    jadro.sciezka_pliku[0] =
        '\0';

    ustaw_stan_procesu(
        jadro,
        PROCES_GOTOWY
    );

    aktualny_pid = 0;
    aktywny_proces = jadro;

    ustaw_stos_jadra_tss(
        kernel_rsp0
    );

    bezpieczny_stos_jadra =
        kernel_rsp0;

    przywroc_przerwania(
        stan_irq
    );
}

/* =========================================================================
 * PRZELACZENIE KONTEKSTU
 * ========================================================================= */

extern "C" uint64_t PrzelaczKontekst(
    uint64_t stary_rsp
) {
    /*
     * Funkcja jest wywolywana z interrupt gate - IF jest juz wyzerowane.
     * Nie wykonujemy tutaj STI.
     */
    if (stary_rsp == 0) {
        return 0;
    }

    if (!__atomic_load_n(
            &wielozadaniowosc_aktywna,
            __ATOMIC_ACQUIRE)) {

        return stary_rsp;
    }

    if (!pid_poprawny(
            aktualny_pid)) {

        /*
         * Nie indeksujemy tablicy uszkodzonym PID.
         */
        return stary_rsp;
    }

    const int stary_pid =
        aktualny_pid;

    proces_t& stary_proces =
        tablica_procesow[
            stary_pid];

    /*
     * Zapisujemy rame nawet dla procesu zablokowanego. Po wybudzeniu musi
     * wrocic dokladnie do miejsca przerwania w Ring 3.
     *
     * Proces PUSTY zostal zakonczony i nie potrzebuje juz ramy.
     */
    const int stan_starego=odczytaj_stan_procesu(stary_proces);
    if (stan_starego != PROCES_PUSTY && stan_starego != PROCES_KONCZACY) {

        stary_proces.kernel_rsp =
            stary_rsp;
    }

    /*
     * Nie wolno sprzatac zasobow stary_pid, bo nadal wykonujemy funkcje
     * na jego kernel stack. Sprzatamy jedynie starsze zakonczone procesy.
     */
    posprzataj_zakonczone_procesy(
        stary_pid
    );

    ++licznik_tykniec_zegara;

    obsluz_timeouty_myszy();

    const int nastepny_pid =
        znajdz_nastepny_proces(
            stary_pid
        );

    if (!pid_poprawny(
            nastepny_pid)) {

        /*
         * PID0 powinien zawsze stanowic bezpieczny idle fallback.
         * Jesli nawet on nie ma prawidlowej ramy, pozostajemy na obecnej.
         */
        return stary_rsp;
    }

    /*
     * Jezeli round-robin wybral ten sam proces, nie wykonujemy
     * niepotrzebnego przelaczenia CR3/TSS.
     */
    if (nastepny_pid ==
        stary_pid) {

        aktywny_proces =
            stary_proces;

        return stary_rsp;
    }

    proces_t& nastepny =
        tablica_procesow[
            nastepny_pid];

    if (!proces_moze_byc_uruchomiony(
            nastepny_pid)) {

        return stary_rsp;
    }

    const uint64_t nowy_rsp =
        nastepny.kernel_rsp;

    const uint64_t nowy_rsp0 =
        nastepny.szczyt_stosu_jadra;

    const uint64_t nowy_cr3 =
        nastepny.cr3 &
        0x000FFFFFFFFFF000ULL;

    if (nowy_rsp == 0 ||
        nowy_rsp0 == 0 ||
        !cr3_poprawne(
            nowy_cr3)) {

        return stary_rsp;
    }

    /*
     * TSS.rsp0 i stos bramy SYSCALL musza wskazywac NOWY proces zanim
     * wykonamy iretq do jego Ring 3.
     */
    ustaw_stos_jadra_tss(
        nowy_rsp0
    );

    bezpieczny_stos_jadra =
        nowy_rsp0;

    /*
     * CR3 przelaczamy jeszcze na starym stosie Ring 0.
     * Wszystkie stosy Ring 0 sa zaalokowane w globalnej przestrzeni jadra,
     * wiec pozostaja dostepne po zmianie PML4.
     */
    const uint64_t obecny_cr3 =
        odczytaj_cr3();

    if (nowy_cr3 !=
        obecny_cr3) {

        UstawPrzestrzenAdresowa(
            reinterpret_cast<void*>(
                nowy_cr3
            )
        );

        /*
         * VMM ma API void, wiec sprawdzamy wynik sprzetowo.
         */
        if (odczytaj_cr3() !=
            nowy_cr3) {

            /*
             * Nie publikujemy nastepnego PID, jezeli CR3 nie zostalo
             * ustawione.
             */
            ustaw_stos_jadra_tss(
                stary_proces.
                    szczyt_stosu_jadra
            );

            bezpieczny_stos_jadra =
                stary_proces.
                    szczyt_stosu_jadra;

            return stary_rsp;
        }
    }

    /*
     * Publikacja aktualnego procesu jest ostatnim krokiem po udanej
     * walidacji i zmianie CR3.
     */
    aktualny_pid =
        nastepny_pid;

    ++licznik_przelaczen_kontekstu;

    aktywny_proces =
        nastepny;

    return nowy_rsp;
}

/* =========================================================================
 * ZAKONCZENIE PROCESU
 * ========================================================================= */

extern "C" void zakoncz_aktualny_proces() {
    /*
     * Ta funkcja jest wywolywana z BWS/SYSCALL i nie moze bezpiecznie
     * zwolnic wlasnego stosu ani PML4.
     */
    asm volatile(
        "cli"
        :
        :
        : "memory"
    );

    const int pid =
        aktualny_pid;

    if (!pid_uzytkownika(
            pid)) {

        /*
         * PID 0 nie moze zakonczyc sie przez API procesu uzytkownika.
         */
        for (;;) {
            asm volatile(
                "hlt"
                :
                :
                : "memory"
            );
        }
    }

    proces_t& proces =
        tablica_procesow[pid];

    scheduler_usun_zdarzenia_procesu(pid);

    ZasobyDoZwolnienia& zasoby =
        zasoby_do_zwolnienia[pid];

    /*
     * Podwojne zakonczenie tego samego aktywnego procesu oznacza blad
     * logiki. Nie nadpisujemy snapshotu zasobow.
     */
    if (!zasoby.oczekuje) {
        zasoby.przestrzen_adresowa =
            proces.przestrzen_adresowa;

        zasoby.baza_stosu_jadra =
            reinterpret_cast<void*>(
                proces.baza_stosu_jadra
            );

        zasoby.oczekuje = true;
    }

    /*
     * PROCES_PUSTY jest istotny dla idt.cpp:
     * timer przerwany w Ring 0 moze wtedy wywolac PrzelaczKontekst(),
     * mimo ze normalnie nie zmieniamy procesu wewnatrz kodu kernela.
     */
    proces.poziom_zaufania =
        PZB_PIASKOWNICA;

    proces.uprawnienia =
        PRAWA_BRAK;

    ustaw_stan_procesu(
        proces,
        PROCES_KONCZACY
    );

    aktywny_proces =
        proces;

    /*
     * Czekamy tylko na najblizszy timer.
     *
     * W przeciwienstwie do starego oczekiwania na mysz jest to poprawne:
     * idt.cpp specjalnie zezwala na przelaczenie procesu Ring 0, gdy jego
     * stan to PROCES_PUSTY.
     *
     * Po zmianie kontekstu wykonanie nigdy nie wroci na ten stos.
     */
    for (;;) {
        asm volatile(
            "sti\n\t"
            "hlt\n\t"
            "cli"
            :
            :
            : "memory", "cc"
        );
    }
}

/* =========================================================================
 * KONTROLA SINGLE-INSTANCE
 * ========================================================================= */

extern "C" bool czy_proces_uruchomiony(
    const char* sciezka
) {
    /*
     * To API jest przeznaczone dla wskaznika zaufanego kernela.
     * Loader z Ring 3 najpierw kopiuje sciezke do bufora kernela.
     */
    if (!sciezka_wejsciowa_poprawna(
            sciezka)) {

        return false;
    }

    for (int pid = 1;
         pid < MAKS_PROCESOW;
         ++pid) {

        const proces_t& proces =
            tablica_procesow[pid];

        /*
         * PROCES_ZABLOKOWANY jest uzywany przez loader jako rezerwacja
         * slotu podczas budowania procesu. Traktujemy kazdy niepusty stan
         * jako istniejaca instancje.
         */
        const int stan=odczytaj_stan_procesu(proces);
        if (stan==PROCES_PUSTY || stan==PROCES_KONCZACY) {

            continue;
        }

        if (sciezki_rowne(
                proces.sciezka_pliku,
                sciezka)) {

            return true;
        }
    }

    return false;
}
