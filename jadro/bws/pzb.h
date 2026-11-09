/*
 * Bursztyn OS - Poziomy Zaufania Bursztyna (PZB)
 *
 * Publiczne definicje polityki zaufania, masek uprawnien oraz podstawowego
 * deskryptora procesu uzywanego przez loader, scheduler i BWS.
 *
 * Model bezpieczenstwa:
 *
 *   PZB 0  - jadro
 *   PZB 1  - sterowniki
 *   PZB 2  - uslugi systemowe
 *   PZB 3  - aplikacje zaufane
 *   PZB 4  - zwykle aplikacje uzytkownika
 *   PZB 5  - piaskownica
 *
 * Mniejsza liczba oznacza WYZSZY poziom zaufania.
 *
 * WAZNE:
 * Poziom zaufania i maska uprawnien sa dwoma niezaleznymi mechanizmami.
 * Sam wysoki poziom zaufania nie powinien automatycznie nadawac prawa do
 * operacji. BWS powinno sprawdzac wymagane PRAWO_* dla konkretnej operacji,
 * a PZB wykorzystywac jako dodatkowa granice polityki.
 *
 * Struktura proces_t jest wspoldzielona przez kilka krytycznych modulow.
 * Jej uklad zostaje zachowany i jest kontrolowany static_assertami.
 */

#pragma once

#include <stdint.h>
#include <stddef.h>

/* =========================================================================
 * 1. POZIOMY ZAUFANIA PZB
 * ========================================================================= */

/*
 * Zachowujemy stale preprocesora dla zgodnosci z istniejacym kodem
 * kernela, loadera i manifestow aplikacji.
 */
#define PZB_JADRO       0U
#define PZB_STEROWNIKI  1U
#define PZB_USLUGI      2U
#define PZB_ZAUFANE     3U
#define PZB_UZYTKOWNIK  4U
#define PZB_PIASKOWNICA 5U

#define PZB_MIN_POZIOM PZB_JADRO
#define PZB_MAX_POZIOM PZB_PIASKOWNICA
#define PZB_LICZBA_POZIOMOW 6U

/* =========================================================================
 * 2. MASKI UPRAWNIEN
 * ========================================================================= */

/*
 * Kazde prawo jest jawnie 64-bitowe.
 *
 * Stara forma:
 *
 *     (1 << 7)
 *
 * operowala na typie int. Przy dalszym rozszerzaniu listy praw powyzej
 * bitu 31 mogloby to prowadzic do niezdefiniowanego lub blednego wyniku.
 */
#define PRAWO_PLIKI_CZYTAJ    (UINT64_C(1) << 0)
#define PRAWO_PLIKI_ZAPISZ    (UINT64_C(1) << 1)
#define PRAWO_SIEC            (UINT64_C(1) << 2)
#define PRAWO_GUI             (UINT64_C(1) << 3)
#define PRAWO_URUCHOM_PROGRAM (UINT64_C(1) << 4)
#define PRAWO_SYSTEM_CONFIG   (UINT64_C(1) << 5)
#define PRAWO_STEROWNIK       (UINT64_C(1) << 6)
#define PRAWO_DEBUG           (UINT64_C(1) << 7)

/*
 * Brak praw.
 */
#define PRAWA_BRAK UINT64_C(0)

/*
 * Wszystkie prawa zdefiniowane przez obecna wersje PZB.
 *
 * Loader lub parser manifestu powinien odrzucac nieznane bity zamiast
 * zachowywac je "na przyszlosc". Zapobiega to sytuacji, w ktorej stary
 * manifest nieoczekiwanie otrzyma znaczenie po dodaniu nowego prawa.
 */
#define PRAWA_ZNANE \
    (PRAWO_PLIKI_CZYTAJ    | \
     PRAWO_PLIKI_ZAPISZ    | \
     PRAWO_SIEC            | \
     PRAWO_GUI             | \
     PRAWO_URUCHOM_PROGRAM | \
     PRAWO_SYSTEM_CONFIG   | \
     PRAWO_STEROWNIK       | \
     PRAWO_DEBUG)

/*
 * Prawa o szczegolnie wysokim ryzyku.
 *
 * To tylko pomocnicza maska polityki - nie nadaje ani nie odbiera praw
 * automatycznie.
 */
#define PRAWA_UPRZYWILEJOWANE \
    (PRAWO_SYSTEM_CONFIG | \
     PRAWO_STEROWNIK     | \
     PRAWO_DEBUG)

/* =========================================================================
 * 3. STALE DESKRYPTORA PROCESU
 * ========================================================================= */

/*
 * Sciezka jest obecnie uzywana m.in. do blokady wielu instancji programu.
 * Loader przyjmuje maksymalnie 63 bajty tekstu + '\0'.
 */
#define PZB_DLUGOSC_SCIEZKI_PROCESU 64U

/*
 * Obecna tablica schedulera ma 16 slotow.
 *
 * PID 0 jest zarezerwowany dla kontekstu jadra/idle, dlatego programy
 * Ring 3 korzystaja z PID 1..15.
 *
 * W scheduler.h powinno pozostac:
 *
 *     MAKS_PROCESOW == PZB_MAKS_PROCESOW
 *
 * Jezeli limit procesow zostanie zmieniony, oba moduly trzeba zaktualizowac
 * razem.
 */
#define PZB_MAKS_PROCESOW 16U

/* =========================================================================
 * 4. DESKRYPTOR PROCESU
 * ========================================================================= */

/*
 * Nie uzywamy __attribute__((packed)).
 *
 * Ta struktura jest intensywnie uzywana przez kod x86_64 i naturalne
 * wyrownanie pol 64-bitowych jest bezpieczniejsze oraz szybsze.
 *
 * Uklad ABI x86_64:
 *
 *   0x00  uint64_t pid
 *   0x08  uint8_t  poziom_zaufania
 *         7 bajtow paddingu
 *   0x10  uint64_t uprawnienia
 *   0x18  void*    przestrzen_adresowa
 *   0x20  uint64_t kernel_rsp
 *   0x28  uint64_t baza_stosu_jadra
 *   0x30  uint64_t szczyt_stosu_jadra
 *   0x38  uint64_t cr3
 *   0x40  uint64_t granica_sterty
 *   0x48  char     sciezka_pliku[64]
 *   0x88  int      stan
 *   0x8C  4 bajty paddingu koncowego
 *
 * Razem: 0x90 = 144 bajty.
 */
typedef struct proces {
    /*
     * Identyfikator procesu.
     * PID 0 jest kontekstem jadra/idle.
     */
    uint64_t pid;

    /*
     * Bursztynowy Poziom Zaufania 0..5.
     */
    uint8_t poziom_zaufania;

    /*
     * Jawna maska PRAWO_*.
     */
    uint64_t uprawnienia;

    /*
     * Bazowa hierarchia tablic stron procesu.
     *
     * W obecnym VMM adres wartosci jest zgodny z fizycznym adresem PML4
     * dzieki identity/direct dostepowi do ramek tablic stron.
     */
    void* przestrzen_adresowa;

    /*
     * Wskaznik zapisanej RejestryStanowe na prywatnym stosie Ring 0.
     * Scheduler przekazuje go do przerwania/iretq.
     */
    uint64_t kernel_rsp;

    /*
     * Poczatek alokacji prywatnego stosu Ring 0.
     * Potrzebny do diagnostyki i poprawnego zwolnienia procesu.
     */
    uint64_t baza_stosu_jadra;

    /*
     * Szczyt prywatnego stosu Ring 0.
     * Uzywany jako TSS.rsp0 i stos wejscia z Ring 3.
     */
    uint64_t szczyt_stosu_jadra;

    /*
     * Fizyczny adres PML4 wpisywany do CR3 podczas przelaczenia kontekstu.
     */
    uint64_t cr3;

    /*
     * Pierwszy wolny adres wirtualnej sterty procesu Ring 3.
     */
    uint64_t granica_sterty;

    /*
     * Kanoniczna sciezka obrazu programu.
     * Pole musi byc zawsze zakonczone '\0'.
     */
    char sciezka_pliku[
        PZB_DLUGOSC_SCIEZKI_PROCESU
    ];

    /*
     * Stan procesu jest definiowany przez scheduler.h
     * (np. PROCES_PUSTY / PROCES_GOTOWY / PROCES_ZABLOKOWANY).
     *
     * Pozostaje typu int dla zgodnosci ABI z obecnym schedulerem.
     * Loader publikuje to pole atomowo przy zmianie stanu.
     */
    int stan;
} proces_t;

/* =========================================================================
 * 5. KONTROLA ABI proces_t
 * ========================================================================= */

#ifdef __cplusplus

static_assert(
    sizeof(void*) == 8,
    "PZB wymaga jadra x86_64"
);

static_assert(
    PZB_MIN_POZIOM == 0U &&
    PZB_MAX_POZIOM == 5U,
    "Zakres poziomow PZB musi pozostac 0..5"
);

static_assert(
    (PRAWA_ZNANE &
     PRAWA_UPRZYWILEJOWANE) ==
    PRAWA_UPRZYWILEJOWANE,
    "Maska praw uprzywilejowanych zawiera niezdefiniowane prawo"
);

static_assert(
    offsetof(proces_t, pid) == 0x00,
    "Nieprawidlowy offset proces_t.pid"
);

static_assert(
    offsetof(proces_t, poziom_zaufania) == 0x08,
    "Nieprawidlowy offset proces_t.poziom_zaufania"
);

static_assert(
    offsetof(proces_t, uprawnienia) == 0x10,
    "Nieprawidlowy offset proces_t.uprawnienia"
);

static_assert(
    offsetof(proces_t, przestrzen_adresowa) == 0x18,
    "Nieprawidlowy offset proces_t.przestrzen_adresowa"
);

static_assert(
    offsetof(proces_t, kernel_rsp) == 0x20,
    "Nieprawidlowy offset proces_t.kernel_rsp"
);

static_assert(
    offsetof(proces_t, baza_stosu_jadra) == 0x28,
    "Nieprawidlowy offset proces_t.baza_stosu_jadra"
);

static_assert(
    offsetof(proces_t, szczyt_stosu_jadra) == 0x30,
    "Nieprawidlowy offset proces_t.szczyt_stosu_jadra"
);

static_assert(
    offsetof(proces_t, cr3) == 0x38,
    "Nieprawidlowy offset proces_t.cr3"
);

static_assert(
    offsetof(proces_t, granica_sterty) == 0x40,
    "Nieprawidlowy offset proces_t.granica_sterty"
);

static_assert(
    offsetof(proces_t, sciezka_pliku) == 0x48,
    "Nieprawidlowy offset proces_t.sciezka_pliku"
);

static_assert(
    offsetof(proces_t, stan) == 0x88,
    "Nieprawidlowy offset proces_t.stan"
);

static_assert(
    sizeof(proces_t) == 0x90,
    "Zmiana rozmiaru proces_t wymaga aktualizacji schedulera/loadera"
);

static_assert(
    alignof(proces_t) == 8,
    "proces_t powinien zachowac naturalne wyrownanie x86_64"
);

#endif /* __cplusplus */

/* =========================================================================
 * 6. BEZPIECZNE FUNKCJE POMOCNICZE PZB
 * ========================================================================= */

#ifdef __cplusplus

/*
 * Czy wartosc jest prawidlowym poziomem PZB.
 */
inline constexpr bool pzb_poziom_poprawny(
    uint8_t poziom
) noexcept {
    return
        poziom <=
        static_cast<uint8_t>(
            PZB_MAX_POZIOM
        );
}

/*
 * Czy maska nie zawiera bitow nieznanych obecnej wersji jadra.
 */
inline constexpr bool pzb_maska_praw_poprawna(
    uint64_t prawa
) noexcept {
    return
        (prawa &
         ~static_cast<uint64_t>(
             PRAWA_ZNANE)) == 0;
}

/*
 * Czy proces posiada WSZYSTKIE wymagane prawa.
 *
 * wymagane == 0 zwraca true.
 */
inline constexpr bool pzb_ma_prawa(
    const proces_t* proces,
    uint64_t wymagane
) noexcept {
    return
        proces != nullptr &&
        pzb_maska_praw_poprawna(
            proces->uprawnienia) &&
        (proces->uprawnienia &
         wymagane) ==
            wymagane;
}

/*
 * Czy proces posiada co najmniej jedno prawo z podanej maski.
 *
 * Dla maski 0 wynik jest false, co zapobiega przypadkowemu traktowaniu
 * pustego zbioru jako pozytywnej autoryzacji.
 */
inline constexpr bool pzb_ma_jakiekolwiek_prawo(
    const proces_t* proces,
    uint64_t maska
) noexcept {
    return
        proces != nullptr &&
        maska != 0 &&
        pzb_maska_praw_poprawna(
            proces->uprawnienia) &&
        (proces->uprawnienia &
         maska) != 0;
}

/*
 * Porownanie zaufania.
 *
 * Poniewaz 0 oznacza najwyzsze zaufanie:
 *
 *   pzb_ma_co_najmniej_poziom(proces, PZB_USLUGI)
 *
 * jest prawdziwe dla poziomow 0, 1 i 2.
 *
 * Funkcja NIE zastępuje kontroli PRAWO_*.
 */
inline constexpr bool pzb_ma_co_najmniej_poziom(
    const proces_t* proces,
    uint8_t maksymalny_numer_poziomu
) noexcept {
    return
        proces != nullptr &&
        pzb_poziom_poprawny(
            proces->poziom_zaufania) &&
        pzb_poziom_poprawny(
            maksymalny_numer_poziomu) &&
        proces->poziom_zaufania <=
            maksymalny_numer_poziomu;
}

/*
 * Minimalna kontrola spojnosci deskryptora procesu.
 *
 * Nie sprawdza mapowania PML4 ani poprawnosci stanu schedulera - te
 * informacje wymagaja odpowiednich modulow VMM/schedulera.
 */
inline bool pzb_podstawowy_deskryptor_poprawny(
    const proces_t* proces
) noexcept {
    if (!proces) {
        return false;
    }

    if (!pzb_poziom_poprawny(
            proces->poziom_zaufania)) {
        return false;
    }

    if (!pzb_maska_praw_poprawna(
            proces->uprawnienia)) {
        return false;
    }

    /*
     * PID 0 moze byc specjalnym procesem jadra bez pliku i bez prywatnego
     * PML4. Dla zwyklego procesu wymagamy podstawowych zasobow.
     */
    if (proces->pid != 0) {
        if (!proces->przestrzen_adresowa ||
            proces->cr3 == 0 ||
            proces->baza_stosu_jadra == 0 ||
            proces->szczyt_stosu_jadra == 0) {

            return false;
        }

        if (proces->szczyt_stosu_jadra <=
            proces->baza_stosu_jadra) {

            return false;
        }

        /*
         * Sciezka musi miec NUL wewnatrz bufora.
         */
        bool zakonczona = false;

        for (size_t i = 0;
             i <
                static_cast<size_t>(
                    PZB_DLUGOSC_SCIEZKI_PROCESU);
             ++i) {

            if (proces->sciezka_pliku[i] == '\0') {
                zakonczona = true;
                break;
            }
        }

        if (!zakonczona) {
            return false;
        }
    }

    return true;
}

#endif /* __cplusplus */

/* =========================================================================
 * 7. GLOBALNY STAN PROCESOW
 * ========================================================================= */

/*
 * aktywny_proces jest zachowany dla kompatybilnosci ze starszym kodem.
 * Docelowym zrodlem prawdy schedulera powinna byc tablica_procesow[]
 * wraz z aktualny_pid.
 */
extern proces_t aktywny_proces;

/*
 * Rozmiar tablicy pozostaje zgodny z obecnym schedulerem.
 */
extern proces_t tablica_procesow[
    PZB_MAKS_PROCESOW
];
