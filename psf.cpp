/*
 * Bursztyn OS - Polski System Plikow BSP2 / PSF
 *
 * Menedzer systemu plikow dzialajacy na ramdysku z opcjonalnym
 * write-through do glownego dysku AHCI.
 *
 * Najwazniejsze zalozenia:
 *  - format BSP2 pozostaje zgodny z obecnym psf.h,
 *  - superblok znajduje sie w bloku 0,
 *  - bitmapa blokow danych zaczyna sie od bloku 1,
 *  - wezly indeksowe sa przechowywane od start_wezlow,
 *  - dane plikow i katalogow zaczynaja sie od start_danych,
 *  - wskazniki_blokow[] zawieraja RELATYWNE numery blokow danych,
 *  - id wezla 0 oznacza brak wezla,
 *  - BARK_BLOKU oznacza brak wskaznika do bloku danych.
 *
 * Bezpieczenstwo:
 *  - uszkodzony istniejacy BSP2 NIE jest automatycznie formatowany,
 *  - wszystkie adresy blokow i wezlow sa sprawdzane przed dereferencja,
 *  - sciezki i nazwy nie sa po cichu obcinane,
 *  - niepustego katalogu nie mozna usunac,
 *  - zapis pliku jest przygotowywany przed publikacja nowego rozmiaru,
 *  - niepotrzebne bloki sa zwalniane po skroceniu pliku,
 *  - zapis na dysk obejmuje tylko zmienione bloki PSF.
 *
 * Aktualny format nie posiada jeszcze dziennika transakcyjnego (journal).
 * Awaria zasilania pomiedzy zapisami kilku blokow nadal moze wymagac
 * przyszlego narzedzia fsck. Kod minimalizuje jednak zakres kazdego zapisu
 * i przed montowaniem rygorystycznie sprawdza strukture systemu plikow.
 */

#include "psf.h"
#include "ahci.h"

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/*
 * Logger z podsystemu grafiki.
 */
void wypisz_log(const char* tekst);

namespace {

/* =========================================================================
 * STALE
 * ========================================================================= */

constexpr uint64_t ROZMIAR_SEKTORA = 512ULL;

/*
 * Zachowujemy dotychczasowe polozenie BSP2 na dysku.
 *
 * 40000 sektorow * 512 B = 20 480 000 B (~19.53 MiB).
 *
 * Poprzedni komentarz mowil o ~5 MiB, ale zmiana LBA bez migratora
 * uniemozliwilaby odczyt istniejacych instalacji BSP2.
 */
constexpr uint64_t BSP_START_LBA = 40000ULL;

constexpr uint32_t AHCI_MAX_SEKTOROW_NA_OPERACJE = 32U;

constexpr uint64_t MAX_LOADER_BUF =
    512ULL * 1024ULL;

constexpr size_t MAKS_DLUGOSC_SCIEZKI = 512;

constexpr size_t MAKS_BRUDNYCH_BLOKOW = 256;

/*
 * Minimalna liczba blokow wymagana do sensownego podzialu:
 * superblok + bitmapa + wezly + dane.
 */
constexpr uint64_t MINIMALNA_LICZBA_BLOKOW = 10;

/* =========================================================================
 * STAN GLOBALNY
 * ========================================================================= */

uint8_t* ram_dysk = nullptr;
uint32_t rozmiar_ram_dysku = 0;

superblok* dysk_superblok = nullptr;

uint64_t calkowita_liczba_wezlow = 0;
uint64_t calkowita_liczba_blokow_danych = 0;

uint64_t ostatni_szukany_blok_danych = 0;

bool psf_gotowy = false;

/*
 * AHCI moze byc niedostepne. BSP2 nadal dziala wtedy jako ramdysk.
 *
 * zapis_trwaly_dozwolony jest celowo ustawiany na false po wykryciu
 * uszkodzonego istniejacego BSP2. Chroni to dane na dysku przed
 * automatycznym nadpisaniem nowym formatem.
 */
bool nosnik_ahci_dostepny = false;
bool zapis_trwaly_dozwolony = false;
bool ostatni_flush_ok = true;

/*
 * Dirty tracking.
 */
uint64_t brudne_bloki[MAKS_BRUDNYCH_BLOKOW] = {};
size_t liczba_brudnych_blokow = 0;
bool brudny_caly_system = false;

/*
 * API loadera zachowuje dotychczasowy kontrakt zwracania statycznego
 * wskaznika. Bufor jest chroniony podczas wypelniania przez blokade PSF.
 *
 * Docelowo lepszym ABI bedzie:
 *   bool bsp_wczytaj_plik(..., void* dst, size_t cap, size_t* out)
 * albo obiekt/refcount, aby calkowicie usunac wspoldzielony bufor.
 */
alignas(16)
uint8_t bufor_wymiany_plikow[MAX_LOADER_BUF] = {};

/*
 * Sektor do wstepnego odczytu superbloku.
 */
alignas(512)
uint8_t bufor_superbloku[ROZMIAR_SEKTORA] = {};

/* =========================================================================
 * SYNCHRONIZACJA
 * ========================================================================= */

/*
 * Obecny scheduler Bursztyna jest jednordzeniowy i wywlaszczajacy przez IRQ
 * timera. Sam spinlock nie wystarcza: proces moglby zostac wywlaszczony
 * trzymajac blokade, a drugi proces zapetlilby sie na tym samym CPU.
 *
 * Dlatego cala operacja PSF jest wykonywana z lokalnie wylaczonym IF.
 * To jest bezpieczne funkcjonalnie, ale write-through AHCI wydluza sekcje
 * krytyczna. Gdy pojawi sie scheduler z mutexami/usypianiem, blokade nalezy
 * zastapic mutexem PSF i przeniesc fizyczny flush do osobnego workera.
 */

uint32_t blokada_psf = 0;

struct StanPrzerwan {
    uint64_t rflags;
};

static inline StanPrzerwan wylacz_przerwania() {
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

static void zablokuj_psf() {
    while (__atomic_exchange_n(
               &blokada_psf,
               1U,
               __ATOMIC_ACQUIRE) != 0U) {

        while (__atomic_load_n(
                   &blokada_psf,
                   __ATOMIC_RELAXED) != 0U) {

            asm volatile("pause");
        }
    }
}

static void odblokuj_psf() {
    __atomic_store_n(
        &blokada_psf,
        0U,
        __ATOMIC_RELEASE
    );
}

class BlokadaPSF {
public:
    BlokadaPSF()
        : stan_(wylacz_przerwania()) {
        zablokuj_psf();
    }

    ~BlokadaPSF() {
        odblokuj_psf();
        przywroc_przerwania(stan_);
    }

    BlokadaPSF(const BlokadaPSF&) = delete;
    BlokadaPSF& operator=(const BlokadaPSF&) = delete;

private:
    StanPrzerwan stan_;
};

/* =========================================================================
 * NISKIE FUNKCJE PAMIECIOWE
 * ========================================================================= */

void wyzeruj_pamiec(
    void* wskaznik,
    uint64_t rozmiar
) {
    if (!wskaznik || rozmiar == 0) {
        return;
    }

    uint8_t* p =
        static_cast<uint8_t*>(
            wskaznik
        );

    for (uint64_t i = 0;
         i < rozmiar;
         ++i) {

        p[i] = 0;
    }
}

void kopiuj_pamiec(
    void* cel,
    const void* zrodlo,
    uint64_t rozmiar
) {
    if (rozmiar == 0) {
        return;
    }

    if (!cel || !zrodlo) {
        return;
    }

    uint8_t* dst =
        static_cast<uint8_t*>(
            cel
        );

    const uint8_t* src =
        static_cast<const uint8_t*>(
            zrodlo
        );

    const uintptr_t dst_adres =
        reinterpret_cast<uintptr_t>(dst);

    const uintptr_t src_adres =
        reinterpret_cast<uintptr_t>(src);

    /*
     * memmove-like copy, aby przypadkowe nakladanie zakresow nie
     * uszkadzalo danych. Porownujemy wartosci adresow jako uintptr_t,
     * a nie relacje wskaznikow nalezacych do roznych obiektow.
     */
    if (dst_adres < src_adres) {
        for (uint64_t i = 0;
             i < rozmiar;
             ++i) {

            dst[i] = src[i];
        }
    } else if (dst_adres > src_adres) {
        for (uint64_t i = rozmiar;
             i > 0;
             --i) {

            dst[i - 1] = src[i - 1];
        }
    }
}

bool mnoz_u64(
    uint64_t a,
    uint64_t b,
    uint64_t* wynik
) {
    if (!wynik) {
        return false;
    }

    if (a != 0 &&
        b > UINT64_MAX / a) {
        return false;
    }

    *wynik = a * b;
    return true;
}

bool dodaj_u64(
    uint64_t a,
    uint64_t b,
    uint64_t* wynik
) {
    if (!wynik) {
        return false;
    }

    if (a > UINT64_MAX - b) {
        return false;
    }

    *wynik = a + b;
    return true;
}

uint64_t podziel_w_gore(
    uint64_t wartosc,
    uint64_t dzielnik
) {
    if (dzielnik == 0) {
        return 0;
    }

    return
        wartosc / dzielnik +
        ((wartosc % dzielnik) != 0 ? 1ULL : 0ULL);
}

/* =========================================================================
 * WALIDACJA STALYCH FORMATU
 * ========================================================================= */

bool parametry_formatu_obslugiwane() {
    if (PSF_ROZMIAR_BLOKU == 0) {
        return false;
    }

    if ((static_cast<uint64_t>(
             PSF_ROZMIAR_BLOKU) %
         ROZMIAR_SEKTORA) != 0) {
        return false;
    }

    if (sizeof(superblok) >
        ROZMIAR_SEKTORA) {
        /*
         * Inicjalizacja najpierw czyta tylko jeden sektor, aby dopiero
         * potem bezpiecznie ustalic rozmiar calego systemu plikow.
         */
        return false;
    }

    if (sizeof(wezel_indeksowy) >
        static_cast<size_t>(
            PSF_ROZMIAR_BLOKU)) {
        return false;
    }

    if (sizeof(wpis_katalogowy) >
        static_cast<size_t>(
            PSF_ROZMIAR_BLOKU)) {
        return false;
    }

    if ((PSF_ROZMIAR_BLOKU /
         sizeof(wezel_indeksowy)) == 0) {
        return false;
    }

    if ((PSF_ROZMIAR_BLOKU /
         sizeof(wpis_katalogowy)) == 0) {
        return false;
    }

    return true;
}

/* =========================================================================
 * DIRTY TRACKING I WRITE-THROUGH
 * ========================================================================= */

void wyczysc_liste_brudnych() {
    liczba_brudnych_blokow = 0;
    brudny_caly_system = false;
}

void oznacz_caly_system_jako_brudny() {
    brudny_caly_system = true;
    liczba_brudnych_blokow = 0;
}

void oznacz_blok_jako_brudny(
    uint64_t nr_bloku
) {
    if (!dysk_superblok) {
        return;
    }

    if (nr_bloku >=
        dysk_superblok->ilosc_blokow) {
        return;
    }

    if (brudny_caly_system) {
        return;
    }

    for (size_t i = 0;
         i < liczba_brudnych_blokow;
         ++i) {

        if (brudne_bloki[i] ==
            nr_bloku) {
            return;
        }
    }

    if (liczba_brudnych_blokow >=
        MAKS_BRUDNYCH_BLOKOW) {

        oznacz_caly_system_jako_brudny();
        return;
    }

    brudne_bloki[
        liczba_brudnych_blokow++] =
        nr_bloku;
}

bool zapisz_sektory_porcjami(
    uint64_t lba,
    uint64_t liczba_sektorow,
    uint8_t* dane
) {
    if (!dane) {
        return false;
    }

    uint64_t zapisano = 0;

    while (zapisano <
           liczba_sektorow) {

        uint64_t pozostalo =
            liczba_sektorow -
            zapisano;

        uint32_t paczka =
            pozostalo >
                    AHCI_MAX_SEKTOROW_NA_OPERACJE
                ? AHCI_MAX_SEKTOROW_NA_OPERACJE
                : static_cast<uint32_t>(
                    pozostalo);

        if (!zapisz_na_glowny_dysk_ahci(
                lba + zapisano,
                paczka,
                dane +
                    zapisano *
                    ROZMIAR_SEKTORA)) {

            return false;
        }

        zapisano += paczka;
    }

    return true;
}

bool czytaj_sektory_porcjami(
    uint64_t lba,
    uint64_t liczba_sektorow,
    uint8_t* cel
) {
    if (!cel) {
        return false;
    }

    uint64_t przeczytano = 0;

    while (przeczytano <
           liczba_sektorow) {

        uint64_t pozostalo =
            liczba_sektorow -
            przeczytano;

        uint32_t paczka =
            pozostalo >
                    AHCI_MAX_SEKTOROW_NA_OPERACJE
                ? AHCI_MAX_SEKTOROW_NA_OPERACJE
                : static_cast<uint32_t>(
                    pozostalo);

        if (!czytaj_z_glownego_dysku_ahci(
                lba + przeczytano,
                paczka,
                cel +
                    przeczytano *
                    ROZMIAR_SEKTORA)) {

            return false;
        }

        przeczytano += paczka;
    }

    return true;
}

bool bsp_zapisz_zmiany() {
    if (!ram_dysk ||
        !dysk_superblok) {

        return false;
    }

    /*
     * Brak dysku nie psuje semantyki ramdysku.
     */
    if (!nosnik_ahci_dostepny ||
        !zapis_trwaly_dozwolony) {

        /*
         * W trybie RAM-only nie akumulujemy bez konca listy dirty.
         * Obecne BSP2 nie obsluguje hot-plug/reconnect nosnika.
         */
        wyczysc_liste_brudnych();
        ostatni_flush_ok = false;
        return false;
    }

    if (!brudny_caly_system &&
        liczba_brudnych_blokow == 0) {

        ostatni_flush_ok = true;
        return true;
    }

    const uint64_t sektory_na_blok =
        static_cast<uint64_t>(
            PSF_ROZMIAR_BLOKU) /
        ROZMIAR_SEKTORA;

    bool sukces = true;

    if (brudny_caly_system) {
        const uint64_t liczba_sektorow =
            static_cast<uint64_t>(
                dysk_superblok->
                    calkowity_rozmiar) /
            ROZMIAR_SEKTORA;

        sukces =
            zapisz_sektory_porcjami(
                BSP_START_LBA,
                liczba_sektorow,
                ram_dysk
            );
    } else {
        for (size_t i = 0;
             i < liczba_brudnych_blokow;
             ++i) {

            const uint64_t nr_bloku =
                brudne_bloki[i];

            uint64_t przesuniecie_bajtowe = 0;

            if (!mnoz_u64(
                    nr_bloku,
                    static_cast<uint64_t>(
                        PSF_ROZMIAR_BLOKU),
                    &przesuniecie_bajtowe)) {

                sukces = false;
                break;
            }

            uint64_t przesuniecie_lba = 0;

            if (!mnoz_u64(
                    nr_bloku,
                    sektory_na_blok,
                    &przesuniecie_lba)) {

                sukces = false;
                break;
            }

            if (!zapisz_sektory_porcjami(
                    BSP_START_LBA +
                        przesuniecie_lba,
                    sektory_na_blok,
                    ram_dysk +
                        przesuniecie_bajtowe)) {

                sukces = false;
                break;
            }
        }
    }

    if (sukces) {
        wyczysc_liste_brudnych();
    }

    ostatni_flush_ok = sukces;
    return sukces;
}

/* =========================================================================
 * RAW BLOKI I WEZLY
 * ========================================================================= */

uint8_t* pobierz_blok(
    uint64_t nr_bloku
) {
    if (!ram_dysk ||
        !dysk_superblok) {

        return nullptr;
    }

    if (nr_bloku >=
        dysk_superblok->ilosc_blokow) {

        return nullptr;
    }

    uint64_t offset = 0;

    if (!mnoz_u64(
            nr_bloku,
            static_cast<uint64_t>(
                PSF_ROZMIAR_BLOKU),
            &offset)) {

        return nullptr;
    }

    uint64_t koniec = 0;

    if (!dodaj_u64(
            offset,
            static_cast<uint64_t>(
                PSF_ROZMIAR_BLOKU),
            &koniec)) {

        return nullptr;
    }

    if (koniec >
        static_cast<uint64_t>(
            rozmiar_ram_dysku)) {

        return nullptr;
    }

    return ram_dysk + offset;
}

uint64_t wezlow_na_blok() {
    return
        static_cast<uint64_t>(
            PSF_ROZMIAR_BLOKU) /
        static_cast<uint64_t>(
            sizeof(wezel_indeksowy));
}

uint64_t wpisow_katalogowych_na_blok() {
    return
        static_cast<uint64_t>(
            PSF_ROZMIAR_BLOKU) /
        static_cast<uint64_t>(
            sizeof(wpis_katalogowy));
}

wezel_indeksowy* pobierz_wezel(
    uint64_t id_wezla
) {
    if (!dysk_superblok ||
        id_wezla == 0 ||
        id_wezla >
            calkowita_liczba_wezlow) {

        return nullptr;
    }

    const uint64_t na_blok =
        wezlow_na_blok();

    if (na_blok == 0) {
        return nullptr;
    }

    const uint64_t indeks =
        id_wezla - 1ULL;

    const uint64_t nr_bloku =
        dysk_superblok->
            start_wezlow +
        indeks / na_blok;

    if (nr_bloku >=
        dysk_superblok->
            start_danych) {

        return nullptr;
    }

    uint8_t* blok =
        pobierz_blok(nr_bloku);

    if (!blok) {
        return nullptr;
    }

    wezel_indeksowy* wezly =
        reinterpret_cast<
            wezel_indeksowy*>(
                blok
        );

    return &wezly[
        indeks % na_blok
    ];
}

void oznacz_wezel_jako_brudny(
    uint64_t id_wezla
) {
    if (!dysk_superblok ||
        id_wezla == 0) {

        return;
    }

    const uint64_t na_blok =
        wezlow_na_blok();

    if (na_blok == 0) {
        return;
    }

    const uint64_t indeks =
        id_wezla - 1ULL;

    const uint64_t nr_bloku =
        dysk_superblok->
            start_wezlow +
        indeks / na_blok;

    oznacz_blok_jako_brudny(
        nr_bloku
    );
}

bool id_bloku_danych_poprawne(
    uint64_t id
) {
    return
        id <
        calkowita_liczba_blokow_danych;
}

uint64_t absolutny_blok_danych(
    uint64_t id
) {
    if (!dysk_superblok ||
        !id_bloku_danych_poprawne(id)) {

        return UINT64_MAX;
    }

    return
        dysk_superblok->
            start_danych +
        id;
}

uint8_t* pobierz_blok_danych(
    uint64_t id
) {
    const uint64_t absolutny =
        absolutny_blok_danych(id);

    if (absolutny == UINT64_MAX) {
        return nullptr;
    }

    return pobierz_blok(
        absolutny
    );
}

wpis_katalogowy* pobierz_wpisy_katalogowe(
    uint64_t id_bloku_danych
) {
    uint8_t* blok =
        pobierz_blok_danych(
            id_bloku_danych
        );

    if (!blok) {
        return nullptr;
    }

    return reinterpret_cast<
        wpis_katalogowy*>(
            blok
    );
}

/* =========================================================================
 * BITMAPA BLOKOW DANYCH
 * ========================================================================= */

bool lokalizacja_bitmapy(
    uint64_t id_bloku_danych,
    uint64_t* nr_bloku_bitmapy,
    uint64_t* offset,
    uint8_t* maska
) {
    if (!dysk_superblok ||
        !nr_bloku_bitmapy ||
        !offset ||
        !maska) {

        return false;
    }

    if (!id_bloku_danych_poprawne(
            id_bloku_danych)) {

        return false;
    }

    const uint64_t bajt =
        id_bloku_danych / 8ULL;

    const uint64_t blok =
        1ULL +
        bajt /
        static_cast<uint64_t>(
            PSF_ROZMIAR_BLOKU);

    if (blok >=
        dysk_superblok->
            start_wezlow) {

        return false;
    }

    *nr_bloku_bitmapy =
        blok;

    *offset =
        bajt %
        static_cast<uint64_t>(
            PSF_ROZMIAR_BLOKU);

    *maska =
        static_cast<uint8_t>(
            1U <<
            static_cast<uint8_t>(
                id_bloku_danych &
                7ULL)
        );

    return true;
}

bool czy_blok_danych_zajety(
    uint64_t id_bloku_danych
) {
    uint64_t nr_bitmapy = 0;
    uint64_t offset = 0;
    uint8_t maska = 0;

    if (!lokalizacja_bitmapy(
            id_bloku_danych,
            &nr_bitmapy,
            &offset,
            &maska)) {

        return false;
    }

    uint8_t* blok =
        pobierz_blok(
            nr_bitmapy
        );

    if (!blok) {
        return false;
    }

    return
        (blok[offset] &
         maska) != 0;
}

bool ustaw_stan_bloku_danych(
    uint64_t id_bloku_danych,
    bool zajety
) {
    uint64_t nr_bitmapy = 0;
    uint64_t offset = 0;
    uint8_t maska = 0;

    if (!lokalizacja_bitmapy(
            id_bloku_danych,
            &nr_bitmapy,
            &offset,
            &maska)) {

        return false;
    }

    uint8_t* blok =
        pobierz_blok(
            nr_bitmapy
        );

    if (!blok) {
        return false;
    }

    if (zajety) {
        blok[offset] |= maska;
    } else {
        blok[offset] &=
            static_cast<uint8_t>(
                ~maska
            );
    }

    oznacz_blok_jako_brudny(
        nr_bitmapy
    );

    return true;
}

uint64_t zaalokuj_wolny_blok_danych() {
    if (!dysk_superblok ||
        calkowita_liczba_blokow_danych == 0) {

        return BARK_BLOKU;
    }

    uint64_t start =
        ostatni_szukany_blok_danych;

    if (start >=
        calkowita_liczba_blokow_danych) {

        start = 0;
    }

    for (uint64_t przebieg = 0;
         przebieg < 2;
         ++przebieg) {

        const uint64_t od =
            przebieg == 0
                ? start
                : 0;

        const uint64_t do_ =
            przebieg == 0
                ? calkowita_liczba_blokow_danych
                : start;

        for (uint64_t id = od;
             id < do_;
             ++id) {

            if (czy_blok_danych_zajety(
                    id)) {

                continue;
            }

            if (!ustaw_stan_bloku_danych(
                    id,
                    true)) {

                continue;
            }

            uint8_t* dane =
                pobierz_blok_danych(
                    id
                );

            if (!dane) {
                (void)ustaw_stan_bloku_danych(
                    id,
                    false
                );
                continue;
            }

            wyzeruj_pamiec(
                dane,
                PSF_ROZMIAR_BLOKU
            );

            const uint64_t absolutny =
                absolutny_blok_danych(
                    id
                );

            if (absolutny != UINT64_MAX) {
                oznacz_blok_jako_brudny(
                    absolutny
                );
            }

            ostatni_szukany_blok_danych =
                id + 1ULL;

            if (ostatni_szukany_blok_danych >=
                calkowita_liczba_blokow_danych) {

                ostatni_szukany_blok_danych = 0;
            }

            return id;
        }
    }

    return BARK_BLOKU;
}

bool zwolnij_blok_danych(
    uint64_t id_bloku_danych
) {
    if (!id_bloku_danych_poprawne(
            id_bloku_danych)) {

        return false;
    }

    if (!czy_blok_danych_zajety(
            id_bloku_danych)) {

        /*
         * Double-free lub uszkodzone metadane.
         */
        return false;
    }

    uint8_t* dane =
        pobierz_blok_danych(
            id_bloku_danych
        );

    if (!dane) {
        return false;
    }

    /*
     * Zerowanie przy zwalnianiu ogranicza ryzyko odzyskania danych
     * starego pliku po ponownej alokacji.
     */
    wyzeruj_pamiec(
        dane,
        PSF_ROZMIAR_BLOKU
    );

    const uint64_t absolutny =
        absolutny_blok_danych(
            id_bloku_danych
        );

    if (absolutny != UINT64_MAX) {
        oznacz_blok_jako_brudny(
            absolutny
        );
    }

    if (!ustaw_stan_bloku_danych(
            id_bloku_danych,
            false)) {

        return false;
    }

    if (id_bloku_danych <
        ostatni_szukany_blok_danych) {

        ostatni_szukany_blok_danych =
            id_bloku_danych;
    }

    return true;
}

/* =========================================================================
 * WEZLY
 * ========================================================================= */

void wyzeruj_wezel(
    wezel_indeksowy* w,
    uint8_t typ
) {
    if (!w) {
        return;
    }

    /*
     * Nie zakladamy, ze TYP_WOLNY == 0.
     */
    wyzeruj_pamiec(
        w,
        sizeof(wezel_indeksowy)
    );

    w->typ = typ;
    w->rozmiar_w_bajtach = 0;
    w->czas_utworzenia = 0;
    w->flagi_zabezpieczen = 0;

    for (size_t k = 0;
         k <
            static_cast<size_t>(
                PSF_MAX_BLOKOW_W_WEZLE);
         ++k) {

        w->wskazniki_blokow[k] =
            BARK_BLOKU;
    }

    w->blok_posredni_1 =
        BARK_BLOKU;

    w->blok_posredni_2 =
        BARK_BLOKU;

    w->blok_posredni_3 =
        BARK_BLOKU;
}

uint64_t zaalokuj_wolny_wezel(
    uint8_t typ
) {
    for (uint64_t id = 1;
         id <=
            calkowita_liczba_wezlow;
         ++id) {

        wezel_indeksowy* w =
            pobierz_wezel(id);

        if (!w ||
            w->typ != TYP_WOLNY) {

            continue;
        }

        wyzeruj_wezel(
            w,
            typ
        );

        oznacz_wezel_jako_brudny(
            id
        );

        return id;
    }

    return 0;
}

bool zwolnij_wezel(
    uint64_t id
) {
    wezel_indeksowy* w =
        pobierz_wezel(id);

    if (!w ||
        w->typ == TYP_WOLNY) {

        return false;
    }

    wyzeruj_wezel(
        w,
        TYP_WOLNY
    );

    oznacz_wezel_jako_brudny(
        id
    );

    return true;
}

/* =========================================================================
 * BEZPIECZNE NAZWY I SCIEZKI
 * ========================================================================= */

bool zakonczony_string_w_limicie(
    const char* tekst,
    size_t limit,
    size_t* dlugosc_wyj
) {
    if (!tekst ||
        limit == 0) {

        return false;
    }

    for (size_t i = 0;
         i < limit;
         ++i) {

        if (tekst[i] == '\0') {
            if (dlugosc_wyj) {
                *dlugosc_wyj = i;
            }

            return true;
        }
    }

    return false;
}

bool poprawna_nazwa(
    const char* nazwa
) {
    size_t dlugosc = 0;

    if (!zakonczony_string_w_limicie(
            nazwa,
            static_cast<size_t>(
                PSF_MAX_NAZWA),
            &dlugosc)) {

        return false;
    }

    if (dlugosc == 0 ||
        dlugosc >=
            static_cast<size_t>(
                PSF_MAX_NAZWA)) {

        return false;
    }

    if ((dlugosc == 1 &&
         nazwa[0] == '.') ||
        (dlugosc == 2 &&
         nazwa[0] == '.' &&
         nazwa[1] == '.')) {

        return false;
    }

    for (size_t i = 0;
         i < dlugosc;
         ++i) {

        const char c =
            nazwa[i];

        if (c == '/' ||
            c == '\\' ||
            static_cast<uint8_t>(c) < 0x20U) {

            return false;
        }
    }

    return true;
}

bool bezpieczna_sciezka(
    const char* sciezka
) {
    size_t dlugosc = 0;

    if (!zakonczony_string_w_limicie(
            sciezka,
            MAKS_DLUGOSC_SCIEZKI,
            &dlugosc)) {

        return false;
    }

    if (dlugosc == 0 ||
        sciezka[0] != '/') {

        return false;
    }

    /*
     * "/" jest prawidlowym korzeniem.
     */
    if (dlugosc == 1) {
        return true;
    }

    /*
     * Nie normalizujemy po cichu // ani koncowego /.
     * To eliminuje dwuznacznosci parsera.
     */
    if (sciezka[dlugosc - 1] == '/') {
        return false;
    }

    bool poprzedni_slash = true;

    for (size_t i = 1;
         i < dlugosc;
         ++i) {

        const char c =
            sciezka[i];

        if (c == '/') {
            if (poprzedni_slash) {
                return false;
            }

            poprzedni_slash = true;
        } else {
            poprzedni_slash = false;
        }
    }

    return true;
}

bool kopiuj_nazwe(
    char* cel,
    const char* zrodlo
) {
    if (!cel ||
        !poprawna_nazwa(zrodlo)) {

        return false;
    }

    size_t i = 0;

    for (;
         i <
             static_cast<size_t>(
                 PSF_MAX_NAZWA - 1) &&
         zrodlo[i] != '\0';
         ++i) {

        cel[i] = zrodlo[i];
    }

    cel[i] = '\0';
    return true;
}

bool nazwy_rowne(
    const char* a,
    const char* b
) {
    if (!a || !b) {
        return false;
    }

    for (size_t i = 0;
         i <
            static_cast<size_t>(
                PSF_MAX_NAZWA);
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
     * Brak NUL w polu nazwy na dysku.
     */
    return false;
}

/* =========================================================================
 * KATALOGI
 * ========================================================================= */

struct ZnalezionyWpis {
    bool znaleziony;
    uint64_t id_wezla;
    uint64_t id_bloku_danych;
    uint64_t indeks_wpisu;
};

ZnalezionyWpis znajdz_wpis_w_katalogu(
    uint64_t katalog_id,
    const char* nazwa
) {
    ZnalezionyWpis wynik{};

    if (!poprawna_nazwa(nazwa)) {
        return wynik;
    }

    wezel_indeksowy* katalog =
        pobierz_wezel(
            katalog_id
        );

    if (!katalog ||
        katalog->typ != TYP_KATALOG) {

        return wynik;
    }

    const uint64_t wpisow_na_blok =
        wpisow_katalogowych_na_blok();

    for (size_t k = 0;
         k <
            static_cast<size_t>(
                PSF_MAX_BLOKOW_W_WEZLE);
         ++k) {

        const uint64_t id_bloku =
            katalog->
                wskazniki_blokow[k];

        if (id_bloku ==
            BARK_BLOKU) {

            continue;
        }

        if (!id_bloku_danych_poprawne(
                id_bloku)) {

            return ZnalezionyWpis{};
        }

        wpis_katalogowy* wpisy =
            pobierz_wpisy_katalogowe(
                id_bloku
            );

        if (!wpisy) {
            return ZnalezionyWpis{};
        }

        for (uint64_t j = 0;
             j < wpisow_na_blok;
             ++j) {

            if (wpisy[j].id_wezla == 0) {
                continue;
            }

            if (nazwy_rowne(
                    wpisy[j].nazwa,
                    nazwa)) {

                wynik.znaleziony = true;
                wynik.id_wezla =
                    wpisy[j].id_wezla;
                wynik.id_bloku_danych =
                    id_bloku;
                wynik.indeks_wpisu =
                    j;

                return wynik;
            }
        }
    }

    return wynik;
}

bool katalog_pusty(
    uint64_t katalog_id
) {
    wezel_indeksowy* katalog =
        pobierz_wezel(
            katalog_id
        );

    if (!katalog ||
        katalog->typ != TYP_KATALOG) {

        return false;
    }

    const uint64_t wpisow_na_blok =
        wpisow_katalogowych_na_blok();

    for (size_t k = 0;
         k <
            static_cast<size_t>(
                PSF_MAX_BLOKOW_W_WEZLE);
         ++k) {

        const uint64_t id_bloku =
            katalog->
                wskazniki_blokow[k];

        if (id_bloku ==
            BARK_BLOKU) {

            continue;
        }

        wpis_katalogowy* wpisy =
            pobierz_wpisy_katalogowe(
                id_bloku
            );

        if (!wpisy) {
            return false;
        }

        for (uint64_t j = 0;
             j < wpisow_na_blok;
             ++j) {

            if (wpisy[j].id_wezla != 0) {
                return false;
            }
        }
    }

    return true;
}

bool dodaj_wpis_do_katalogu(
    uint64_t katalog_id,
    uint64_t nowy_twor_id,
    const char* nazwa
) {
    if (!poprawna_nazwa(nazwa)) {
        return false;
    }

    if (!pobierz_wezel(
            nowy_twor_id)) {

        return false;
    }

    if (znajdz_wpis_w_katalogu(
            katalog_id,
            nazwa).znaleziony) {

        return false;
    }

    wezel_indeksowy* katalog =
        pobierz_wezel(
            katalog_id
        );

    if (!katalog ||
        katalog->typ != TYP_KATALOG) {

        return false;
    }

    const uint64_t wpisow_na_blok =
        wpisow_katalogowych_na_blok();

    for (size_t k = 0;
         k <
            static_cast<size_t>(
                PSF_MAX_BLOKOW_W_WEZLE);
         ++k) {

        uint64_t id_bloku =
            katalog->
                wskazniki_blokow[k];

        bool nowo_przydzielony = false;

        if (id_bloku ==
            BARK_BLOKU) {

            id_bloku =
                zaalokuj_wolny_blok_danych();

            if (id_bloku ==
                BARK_BLOKU) {

                return false;
            }

            katalog->
                wskazniki_blokow[k] =
                id_bloku;

            oznacz_wezel_jako_brudny(
                katalog_id
            );

            nowo_przydzielony = true;
        }

        wpis_katalogowy* wpisy =
            pobierz_wpisy_katalogowe(
                id_bloku
            );

        if (!wpisy) {
            if (nowo_przydzielony) {
                katalog->
                    wskazniki_blokow[k] =
                    BARK_BLOKU;

                (void)zwolnij_blok_danych(
                    id_bloku
                );

                oznacz_wezel_jako_brudny(
                    katalog_id
                );
            }

            return false;
        }

        for (uint64_t j = 0;
             j < wpisow_na_blok;
             ++j) {

            if (wpisy[j].id_wezla != 0) {
                continue;
            }

            /*
             * Najpierw nazwa, na koncu id_wezla. W razie inspekcji
             * niedokonczonego RAM stanu wpis z id==0 jest ignorowany.
             */
            if (!kopiuj_nazwe(
                    wpisy[j].nazwa,
                    nazwa)) {

                if (nowo_przydzielony) {
                    katalog->
                        wskazniki_blokow[k] =
                        BARK_BLOKU;

                    (void)zwolnij_blok_danych(
                        id_bloku
                    );

                    oznacz_wezel_jako_brudny(
                        katalog_id
                    );
                }

                return false;
            }

            wpisy[j].id_wezla =
                nowy_twor_id;

            const uint64_t absolutny =
                absolutny_blok_danych(
                    id_bloku
                );

            if (absolutny != UINT64_MAX) {
                oznacz_blok_jako_brudny(
                    absolutny
                );
            }

            katalog->
                rozmiar_w_bajtach++;

            oznacz_wezel_jako_brudny(
                katalog_id
            );

            return true;
        }
    }

    return false;
}

/* =========================================================================
 * PARSER SCIEZEK
 * ========================================================================= */

bool pobierz_segment(
    const char* sciezka,
    size_t* indeks,
    char* segment,
    bool* ostatni
) {
    if (!sciezka ||
        !indeks ||
        !segment ||
        !ostatni) {

        return false;
    }

    size_t pos =
        *indeks;

    if (sciezka[pos] == '\0' ||
        sciezka[pos] == '/') {

        return false;
    }

    size_t dlugosc = 0;

    while (sciezka[pos] != '\0' &&
           sciezka[pos] != '/') {

        if (dlugosc + 1 >=
            static_cast<size_t>(
                PSF_MAX_NAZWA)) {

            return false;
        }

        segment[dlugosc++] =
            sciezka[pos++];
    }

    segment[dlugosc] = '\0';

    if (!poprawna_nazwa(
            segment)) {

        return false;
    }

    if (sciezka[pos] == '\0') {
        *ostatni = true;
        *indeks = pos;
        return true;
    }

    /*
     * bezpieczna_sciezka() zapewnia, ze po slashu istnieje kolejny segment.
     */
    *ostatni = false;
    *indeks = pos + 1;
    return true;
}

uint64_t rozwiaz_sciezke(
    const char* sciezka,
    char* nazwa_docelowa,
    bool zwroc_rodzica
) {
    if (!psf_gotowy ||
        !dysk_superblok ||
        !bezpieczna_sciezka(
            sciezka)) {

        return 0;
    }

    if (sciezka[0] == '/' &&
        sciezka[1] == '\0') {

        /*
         * Korzen nie posiada rodzica/nazwy katalogowej.
         */
        return
            zwroc_rodzica
                ? 0
                : dysk_superblok->
                    id_korzenia;
    }

    if (zwroc_rodzica &&
        !nazwa_docelowa) {

        return 0;
    }

    uint64_t aktualny =
        dysk_superblok->
            id_korzenia;

    size_t indeks = 1;

    while (sciezka[indeks] != '\0') {
        char segment[
            PSF_MAX_NAZWA] = {};

        bool ostatni = false;

        if (!pobierz_segment(
                sciezka,
                &indeks,
                segment,
                &ostatni)) {

            return 0;
        }

        if (ostatni &&
            zwroc_rodzica) {

            if (!kopiuj_nazwe(
                    nazwa_docelowa,
                    segment)) {

                return 0;
            }

            wezel_indeksowy* rodzic =
                pobierz_wezel(
                    aktualny
                );

            if (!rodzic ||
                rodzic->typ !=
                    TYP_KATALOG) {

                return 0;
            }

            return aktualny;
        }

        const ZnalezionyWpis krok =
            znajdz_wpis_w_katalogu(
                aktualny,
                segment
            );

        if (!krok.znaleziony) {
            return 0;
        }

        wezel_indeksowy* cel =
            pobierz_wezel(
                krok.id_wezla
            );

        if (!cel ||
            cel->typ == TYP_WOLNY) {

            return 0;
        }

        if (!ostatni &&
            cel->typ != TYP_KATALOG) {

            return 0;
        }

        aktualny =
            krok.id_wezla;

        if (ostatni) {
            return aktualny;
        }
    }

    return 0;
}

/* =========================================================================
 * WALIDACJA SUPERBLOKU
 * ========================================================================= */

bool sygnatura_bsp2(
    const superblok* sb
) {
    if (!sb) {
        return false;
    }

    return
        sb->sygnatura[0] == 'B' &&
        sb->sygnatura[1] == 'S' &&
        sb->sygnatura[2] == 'P' &&
        sb->sygnatura[3] == '2';
}

bool wylicz_geometrie(
    const superblok* sb,
    uint32_t dostepny_ram,
    uint64_t* wezly,
    uint64_t* bloki_danych
) {
    if (!sb ||
        !wezly ||
        !bloki_danych) {

        return false;
    }

    if (!sygnatura_bsp2(sb)) {
        return false;
    }

    const uint64_t calkowity =
        static_cast<uint64_t>(
            sb->calkowity_rozmiar);

    if (calkowity <
            static_cast<uint64_t>(
                PSF_ROZMIAR_BLOKU) *
            MINIMALNA_LICZBA_BLOKOW ||
        calkowity >
            static_cast<uint64_t>(
                dostepny_ram)) {

        return false;
    }

    if ((calkowity %
         static_cast<uint64_t>(
             PSF_ROZMIAR_BLOKU)) != 0 ||
        (calkowity %
         ROZMIAR_SEKTORA) != 0) {

        return false;
    }

    const uint64_t oczekiwane_bloki =
        calkowity /
        static_cast<uint64_t>(
            PSF_ROZMIAR_BLOKU);

    if (sb->ilosc_blokow !=
        oczekiwane_bloki) {

        return false;
    }

    if (sb->ilosc_blokow <
        MINIMALNA_LICZBA_BLOKOW) {

        return false;
    }

    if (sb->start_wezlow <= 1 ||
        sb->start_wezlow >=
            sb->start_danych ||
        sb->start_danych >=
            sb->ilosc_blokow) {

        return false;
    }

    const uint64_t bloki_bitmapy =
        sb->start_wezlow - 1ULL;

    const uint64_t region_wezlow =
        sb->start_danych -
        sb->start_wezlow;

    const uint64_t region_danych =
        sb->ilosc_blokow -
        sb->start_danych;

    uint64_t bity_bitmapy = 0;

    if (!mnoz_u64(
            bloki_bitmapy,
            static_cast<uint64_t>(
                PSF_ROZMIAR_BLOKU),
            &bity_bitmapy) ||
        !mnoz_u64(
            bity_bitmapy,
            8ULL,
            &bity_bitmapy)) {

        return false;
    }

    if (region_danych >
        bity_bitmapy) {

        return false;
    }

    uint64_t liczba_wezlow = 0;

    if (!mnoz_u64(
            region_wezlow,
            wezlow_na_blok(),
            &liczba_wezlow)) {

        return false;
    }

    if (liczba_wezlow == 0) {
        return false;
    }

    if (sb->id_korzenia == 0 ||
        sb->id_korzenia >
            liczba_wezlow) {

        return false;
    }

    *wezly =
        liczba_wezlow;

    *bloki_danych =
        region_danych;

    return true;
}

/* =========================================================================
 * WALIDACJA METADANYCH PO WCZYTANIU
 * ========================================================================= */

bool wskazniki_wezla_poprawne(
    const wezel_indeksowy* w
) {
    if (!w) {
        return false;
    }

    /*
     * Indirect blocks nie sa jeszcze zaimplementowane przez obecny BSP2.
     * Zamontowanie obrazu, ktory ich uzywa, skonczyloby sie utrata danych
     * podczas modyfikacji, dlatego failujemy bezpiecznie.
     */
    if (w->blok_posredni_1 !=
            BARK_BLOKU ||
        w->blok_posredni_2 !=
            BARK_BLOKU ||
        w->blok_posredni_3 !=
            BARK_BLOKU) {

        return false;
    }

    for (size_t k = 0;
         k <
            static_cast<size_t>(
                PSF_MAX_BLOKOW_W_WEZLE);
         ++k) {

        const uint64_t id =
            w->wskazniki_blokow[k];

        if (id ==
            BARK_BLOKU) {

            continue;
        }

        if (!id_bloku_danych_poprawne(
                id)) {

            return false;
        }

        if (!czy_blok_danych_zajety(
                id)) {

            return false;
        }

        /*
         * Jeden wezel nie moze wskazywac tego samego bloku dwa razy.
         */
        for (size_t j = 0;
             j < k;
             ++j) {

            if (w->wskazniki_blokow[j] ==
                id) {

                return false;
            }
        }
    }

    return true;
}

bool waliduj_katalog(
    uint64_t id,
    const wezel_indeksowy* w
) {
    if (!w ||
        w->typ != TYP_KATALOG) {

        return false;
    }

    const uint64_t wpisow_na_blok =
        wpisow_katalogowych_na_blok();

    uint64_t znalezionych = 0;

    for (size_t k = 0;
         k <
            static_cast<size_t>(
                PSF_MAX_BLOKOW_W_WEZLE);
         ++k) {

        const uint64_t blok =
            w->wskazniki_blokow[k];

        if (blok ==
            BARK_BLOKU) {

            continue;
        }

        wpis_katalogowy* wpisy =
            pobierz_wpisy_katalogowe(
                blok
            );

        if (!wpisy) {
            return false;
        }

        for (uint64_t j = 0;
             j < wpisow_na_blok;
             ++j) {

            if (wpisy[j].id_wezla == 0) {
                continue;
            }

            if (wpisy[j].id_wezla >
                    calkowita_liczba_wezlow ||
                wpisy[j].id_wezla ==
                    id) {

                return false;
            }

            if (!poprawna_nazwa(
                    wpisy[j].nazwa)) {

                return false;
            }

            wezel_indeksowy* cel =
                pobierz_wezel(
                    wpisy[j].id_wezla
                );

            if (!cel ||
                cel->typ == TYP_WOLNY) {

                return false;
            }

            /*
             * Brak duplikatow nazw w jednym katalogu.
             */
            for (size_t k2 = 0;
                 k2 <= k;
                 ++k2) {

                const uint64_t blok2 =
                    w->wskazniki_blokow[k2];

                if (blok2 ==
                    BARK_BLOKU) {

                    continue;
                }

                wpis_katalogowy* wpisy2 =
                    pobierz_wpisy_katalogowe(
                        blok2
                    );

                if (!wpisy2) {
                    return false;
                }

                const uint64_t limit_j =
                    k2 == k
                        ? j
                        : wpisow_na_blok;

                for (uint64_t j2 = 0;
                     j2 < limit_j;
                     ++j2) {

                    if (wpisy2[j2].id_wezla != 0 &&
                        nazwy_rowne(
                            wpisy2[j2].nazwa,
                            wpisy[j].nazwa)) {

                        return false;
                    }
                }
            }

            ++znalezionych;
        }
    }

    /*
     * W aktualnym formacie rozmiar katalogu jest liczba wpisow.
     */
    return
        w->rozmiar_w_bajtach ==
        znalezionych;
}

bool waliduj_metadane() {
    if (!dysk_superblok ||
        calkowita_liczba_wezlow == 0) {

        return false;
    }

    wezel_indeksowy* korzen =
        pobierz_wezel(
            dysk_superblok->
                id_korzenia
        );

    if (!korzen ||
        korzen->typ != TYP_KATALOG) {

        return false;
    }

    uint64_t maks_rozmiar_pliku = 0;

    if (!mnoz_u64(
            static_cast<uint64_t>(
                PSF_MAX_BLOKOW_W_WEZLE),
            static_cast<uint64_t>(
                PSF_ROZMIAR_BLOKU),
            &maks_rozmiar_pliku)) {

        return false;
    }

    for (uint64_t id = 1;
         id <=
            calkowita_liczba_wezlow;
         ++id) {

        wezel_indeksowy* w =
            pobierz_wezel(id);

        if (!w) {
            return false;
        }

        if (w->typ == TYP_WOLNY) {
            continue;
        }

        if (w->typ != TYP_PLIK &&
            w->typ != TYP_KATALOG) {

            return false;
        }

        if (!wskazniki_wezla_poprawne(
                w)) {

            return false;
        }

        if (w->typ == TYP_PLIK) {
            if (w->rozmiar_w_bajtach >
                maks_rozmiar_pliku) {

                return false;
            }

            const uint64_t wymagane =
                podziel_w_gore(
                    w->rozmiar_w_bajtach,
                    PSF_ROZMIAR_BLOKU
                );

            for (uint64_t k = 0;
                 k < wymagane;
                 ++k) {

                if (k >=
                        static_cast<uint64_t>(
                            PSF_MAX_BLOKOW_W_WEZLE) ||
                    w->wskazniki_blokow[k] ==
                        BARK_BLOKU) {

                    return false;
                }
            }
        } else {
            if (!waliduj_katalog(
                    id,
                    w)) {

                return false;
            }
        }
    }

    return true;
}

/* =========================================================================
 * FORMATOWANIE
 * ========================================================================= */

bool sformatuj_ram_dysk(
    bool pozwol_na_zapis_fizyczny
) {
    if (!ram_dysk ||
        rozmiar_ram_dysku == 0 ||
        !parametry_formatu_obslugiwane()) {

        return false;
    }

    const uint64_t rozmiar_uzyteczny =
        (static_cast<uint64_t>(
             rozmiar_ram_dysku) /
         static_cast<uint64_t>(
             PSF_ROZMIAR_BLOKU)) *
        static_cast<uint64_t>(
            PSF_ROZMIAR_BLOKU);

    if (rozmiar_uzyteczny >
            UINT32_MAX ||
        rozmiar_uzyteczny <
            static_cast<uint64_t>(
                PSF_ROZMIAR_BLOKU) *
            MINIMALNA_LICZBA_BLOKOW) {

        return false;
    }

    wyzeruj_pamiec(
        ram_dysk,
        rozmiar_uzyteczny
    );

    dysk_superblok =
        reinterpret_cast<superblok*>(
            ram_dysk
        );

    const uint64_t ilosc_blokow =
        rozmiar_uzyteczny /
        static_cast<uint64_t>(
            PSF_ROZMIAR_BLOKU);

    /*
     * Bitmapa jest nieco przewymiarowana i ma bit dla kazdego bloku FS,
     * mimo ze wykorzystujemy tylko pierwsze calkowita_liczba_blokow_danych
     * bitow. Zachowuje to kompatybilny podzial z dotychczasowym BSP2.
     */
    const uint64_t bajty_bitmapy =
        podziel_w_gore(
            ilosc_blokow,
            8ULL
        );

    const uint64_t bloki_bitmapy =
        podziel_w_gore(
            bajty_bitmapy,
            static_cast<uint64_t>(
                PSF_ROZMIAR_BLOKU)
        );

    if (bloki_bitmapy == 0 ||
        1ULL + bloki_bitmapy >=
            ilosc_blokow) {

        return false;
    }

    const uint64_t start_wezlow =
        1ULL +
        bloki_bitmapy;

    const uint64_t pozostale =
        ilosc_blokow -
        start_wezlow;

    if (pozostale < 2) {
        return false;
    }

    uint64_t bloki_wezlow =
        pozostale / 4ULL;

    if (bloki_wezlow == 0) {
        bloki_wezlow = 1;
    }

    if (bloki_wezlow >=
        pozostale) {

        return false;
    }

    const uint64_t start_danych =
        start_wezlow +
        bloki_wezlow;

    if (start_danych >=
        ilosc_blokow) {

        return false;
    }

    dysk_superblok->
        sygnatura[0] = 'B';

    dysk_superblok->
        sygnatura[1] = 'S';

    dysk_superblok->
        sygnatura[2] = 'P';

    dysk_superblok->
        sygnatura[3] = '2';

    dysk_superblok->
        calkowity_rozmiar =
        static_cast<uint32_t>(
            rozmiar_uzyteczny);

    dysk_superblok->
        ilosc_blokow =
        ilosc_blokow;

    dysk_superblok->
        start_wezlow =
        start_wezlow;

    dysk_superblok->
        start_danych =
        start_danych;

    calkowita_liczba_wezlow =
        bloki_wezlow *
        wezlow_na_blok();

    calkowita_liczba_blokow_danych =
        ilosc_blokow -
        start_danych;

    if (calkowita_liczba_wezlow == 0 ||
        calkowita_liczba_blokow_danych == 0) {

        return false;
    }

    /*
     * Jawnie inicjalizujemy kazdy wezel. Nie zakladamy, ze wartosc
     * TYP_WOLNY jest rowna zeru.
     */
    for (uint64_t id = 1;
         id <=
            calkowita_liczba_wezlow;
         ++id) {

        wezel_indeksowy* w =
            pobierz_wezel(id);

        if (!w) {
            return false;
        }

        wyzeruj_wezel(
            w,
            TYP_WOLNY
        );
    }

    const uint64_t korzen =
        zaalokuj_wolny_wezel(
            TYP_KATALOG
        );

    if (korzen == 0) {
        return false;
    }

    dysk_superblok->
        id_korzenia =
        korzen;

    ostatni_szukany_blok_danych = 0;

    oznacz_caly_system_jako_brudny();

    psf_gotowy = true;

    zapis_trwaly_dozwolony =
        pozwol_na_zapis_fizyczny &&
        nosnik_ahci_dostepny;

    if (zapis_trwaly_dozwolony) {
        (void)bsp_zapisz_zmiany();
    }

    return true;
}

/* =========================================================================
 * WCZYTYWANIE ISTNIEJACEGO BSP2
 * ========================================================================= */

bool wczytaj_istniejacy_bsp2() {
    if (!ram_dysk ||
        rozmiar_ram_dysku == 0) {

        return false;
    }

    wyzeruj_pamiec(
        bufor_superbloku,
        sizeof(bufor_superbloku)
    );

    if (!czytaj_z_glownego_dysku_ahci(
            BSP_START_LBA,
            1,
            bufor_superbloku)) {

        nosnik_ahci_dostepny = false;
        return false;
    }

    nosnik_ahci_dostepny = true;

    superblok kandydat{};

    kopiuj_pamiec(
        &kandydat,
        bufor_superbloku,
        sizeof(superblok)
    );

    if (!sygnatura_bsp2(
            &kandydat)) {

        /*
         * Nosnik odpowiada, ale nie ma BSP2.
         * To bezpieczny przypadek do utworzenia nowego systemu plikow.
         */
        zapis_trwaly_dozwolony = true;
        return false;
    }

    uint64_t wezly = 0;
    uint64_t dane = 0;

    /*
     * Walidacja PRZED uzyciem calkowity_rozmiar jako liczby sektorow.
     */
    if (!wylicz_geometrie(
            &kandydat,
            rozmiar_ram_dysku,
            &wezly,
            &dane)) {

        /*
         * Sygnatura BSP2 istnieje, ale superblok jest uszkodzony.
         * Nie pozwalamy formatowac/niszczyc tego nosnika automatycznie.
         */
        zapis_trwaly_dozwolony = false;
        return false;
    }

    const uint64_t liczba_sektorow =
        static_cast<uint64_t>(
            kandydat.
                calkowity_rozmiar) /
        ROZMIAR_SEKTORA;

    if (!czytaj_sektory_porcjami(
            BSP_START_LBA,
            liczba_sektorow,
            ram_dysk)) {

        zapis_trwaly_dozwolony = false;
        return false;
    }

    dysk_superblok =
        reinterpret_cast<superblok*>(
            ram_dysk
        );

    /*
     * Po DMA sprawdzamy geometrie ponownie. Chroni to przed sytuacja,
     * w ktorej pierwszy sektor i pelny odczyt nie sa spojne.
     */
    if (!wylicz_geometrie(
            dysk_superblok,
            rozmiar_ram_dysku,
            &wezly,
            &dane)) {

        zapis_trwaly_dozwolony = false;
        return false;
    }

    calkowita_liczba_wezlow =
        wezly;

    calkowita_liczba_blokow_danych =
        dane;

    ostatni_szukany_blok_danych = 0;

    psf_gotowy = true;

    if (!waliduj_metadane()) {
        psf_gotowy = false;
        zapis_trwaly_dozwolony = false;
        return false;
    }

    wyczysc_liste_brudnych();

    zapis_trwaly_dozwolony = true;
    ostatni_flush_ok = true;

    return true;
}

/* =========================================================================
 * TWORZENIE PLIKU / KATALOGU
 * ========================================================================= */

bool utworz_twor(
    const char* sciezka,
    uint8_t typ
) {
    if (!psf_gotowy ||
        !bezpieczna_sciezka(
            sciezka)) {

        return false;
    }

    if (rozwiaz_sciezke(
            sciezka,
            nullptr,
            false) != 0) {

        return false;
    }

    char nazwa[
        PSF_MAX_NAZWA] = {};

    const uint64_t rodzic_id =
        rozwiaz_sciezke(
            sciezka,
            nazwa,
            true
        );

    if (rodzic_id == 0 ||
        !poprawna_nazwa(nazwa)) {

        return false;
    }

    wezel_indeksowy* rodzic =
        pobierz_wezel(
            rodzic_id
        );

    if (!rodzic ||
        rodzic->typ != TYP_KATALOG) {

        return false;
    }

    if (znajdz_wpis_w_katalogu(
            rodzic_id,
            nazwa).znaleziony) {

        return false;
    }

    const uint64_t nowy_id =
        zaalokuj_wolny_wezel(
            typ
        );

    if (nowy_id == 0) {
        return false;
    }

    if (!dodaj_wpis_do_katalogu(
            rodzic_id,
            nowy_id,
            nazwa)) {

        (void)zwolnij_wezel(
            nowy_id
        );

        return false;
    }

    /*
     * In-memory commit jest juz wykonany. Blad fizycznego dysku nie
     * odwraca logicznie udanej operacji RAM FS; dirty state pozostaje
     * do ewentualnej kolejnej proby zapisu.
     */
    (void)bsp_zapisz_zmiany();

    return true;
}

/* =========================================================================
 * ZAPIS PLIKU
 * ========================================================================= */

bool wskazniki_pliku_poprawne(
    const wezel_indeksowy* w
) {
    if (!w ||
        w->typ != TYP_PLIK) {

        return false;
    }

    if (w->blok_posredni_1 != BARK_BLOKU ||
        w->blok_posredni_2 != BARK_BLOKU ||
        w->blok_posredni_3 != BARK_BLOKU) {

        return false;
    }

    for (size_t k = 0;
         k <
            static_cast<size_t>(
                PSF_MAX_BLOKOW_W_WEZLE);
         ++k) {

        const uint64_t id =
            w->wskazniki_blokow[k];

        if (id == BARK_BLOKU) {
            continue;
        }

        if (!id_bloku_danych_poprawne(id) ||
            !czy_blok_danych_zajety(id)) {

            return false;
        }

        for (size_t j = 0;
             j < k;
             ++j) {

            if (w->wskazniki_blokow[j] ==
                id) {

                return false;
            }
        }
    }

    return true;
}

/* =========================================================================
 * PUBLICZNE API - INICJALIZACJA
 * ========================================================================= */

} // namespace

extern "C" void inicjalizuj_psf(
    void* adres_ram_dysku,
    uint32_t rozmiar_w_bajtach
) {
    BlokadaPSF blokada;

    /*
     * Reset stanu poprzedniej instancji.
     */
    ram_dysk =
        static_cast<uint8_t*>(
            adres_ram_dysku
        );

    rozmiar_ram_dysku =
        rozmiar_w_bajtach;

    dysk_superblok = nullptr;
    calkowita_liczba_wezlow = 0;
    calkowita_liczba_blokow_danych = 0;
    ostatni_szukany_blok_danych = 0;

    psf_gotowy = false;
    nosnik_ahci_dostepny = false;
    zapis_trwaly_dozwolony = false;
    ostatni_flush_ok = true;

    wyczysc_liste_brudnych();

    if (!ram_dysk ||
        !parametry_formatu_obslugiwane() ||
        rozmiar_w_bajtach <
            static_cast<uint64_t>(
                PSF_ROZMIAR_BLOKU) *
            MINIMALNA_LICZBA_BLOKOW) {

        wypisz_log(
            "[BSP-BLAD] Nieprawidlowy obszar ramdysku lub parametry formatu."
        );
        return;
    }

    wypisz_log(
        "[BSP] Sprawdzanie nosnika AHCI w poszukiwaniu BSP2..."
    );

    /*
     * Najpierw probujemy bezpiecznie zamontowac istniejacy system.
     */
    if (wczytaj_istniejacy_bsp2()) {
        wypisz_log(
            "[BSP] Trwaly BSP2 pomyslnie zaladowany z nosnika SATA."
        );
        return;
    }

    /*
     * Jesli znaleziono sygnature BSP2, ale zapis_trwaly_dozwolony == false
     * przy dostepnym AHCI, traktujemy dysk jako potencjalnie uszkodzony.
     * Tworzymy tylko awaryjny RAM FS - bez nadpisania fizycznego nosnika.
     */
    const bool uszkodzony_istniejacy_bsp2 =
        nosnik_ahci_dostepny &&
        !zapis_trwaly_dozwolony &&
        bufor_superbloku[0] == 'B' &&
        bufor_superbloku[1] == 'S' &&
        bufor_superbloku[2] == 'P' &&
        bufor_superbloku[3] == '2';

    if (uszkodzony_istniejacy_bsp2) {
        wypisz_log(
            "[BSP-BLAD] Wykryto uszkodzony BSP2. Dysk NIE zostanie automatycznie sformatowany."
        );

        if (sformatuj_ram_dysk(false)) {
            wypisz_log(
                "[BSP] Uruchomiono awaryjny system plikow tylko w RAM."
            );
        }

        return;
    }

    if (nosnik_ahci_dostepny) {
        wypisz_log(
            "[BSP] Brak BSP2. Formatowanie nowego systemu plikow."
        );
    } else {
        wypisz_log(
            "[BSP] AHCI niedostepne. Tworzenie systemu plikow tylko w RAM."
        );
    }

    if (!sformatuj_ram_dysk(
            nosnik_ahci_dostepny)) {

        wypisz_log(
            "[BSP-BLAD] Nie mozna utworzyc systemu plikow."
        );
        return;
    }

    if (nosnik_ahci_dostepny &&
        ostatni_flush_ok) {

        wypisz_log(
            "[BSP] Format BSP2 zapisany na nosniku SATA."
        );
    } else if (nosnik_ahci_dostepny) {
        wypisz_log(
            "[BSP] BSP2 dziala w RAM, ale zapis poczatkowy na SATA nie powiodl sie."
        );
    }
}

/* =========================================================================
 * PUBLICZNE API - TWORZENIE
 * ========================================================================= */

extern "C" bool utworz_katalog(
    const char* sciezka
) {
    BlokadaPSF blokada;

    return utworz_twor(
        sciezka,
        TYP_KATALOG
    );
}

extern "C" bool utworz_plik(
    const char* sciezka
) {
    BlokadaPSF blokada;

    return utworz_twor(
        sciezka,
        TYP_PLIK
    );
}

extern "C" bool psf_czy_gotowy() {
    return psf_gotowy;
}

/* =========================================================================
 * PUBLICZNE API - ZAPIS
 * ========================================================================= */

extern "C" bool zapisz_do_pliku(
    const char* sciezka,
    const char* dane,
    uint32_t dlugosc
) {
    BlokadaPSF blokada;

    if (!psf_gotowy ||
        !sciezka ||
        (dlugosc != 0 &&
         !dane)) {

        return false;
    }

    const uint64_t plik_id =
        rozwiaz_sciezke(
            sciezka,
            nullptr,
            false
        );

    if (plik_id == 0) {
        return false;
    }

    wezel_indeksowy* w =
        pobierz_wezel(
            plik_id
        );

    if (!w ||
        w->typ != TYP_PLIK ||
        !wskazniki_pliku_poprawne(
            w)) {

        return false;
    }

    uint64_t maks_rozmiar = 0;

    if (!mnoz_u64(
            static_cast<uint64_t>(
                PSF_MAX_BLOKOW_W_WEZLE),
            static_cast<uint64_t>(
                PSF_ROZMIAR_BLOKU),
            &maks_rozmiar)) {

        return false;
    }

    if (static_cast<uint64_t>(
            dlugosc) >
        maks_rozmiar) {

        /*
         * Brak cichego, czesciowego zapisu.
         */
        return false;
    }

    const uint64_t potrzebne_bloki =
        podziel_w_gore(
            static_cast<uint64_t>(
                dlugosc),
            static_cast<uint64_t>(
                PSF_ROZMIAR_BLOKU)
        );

    /*
     * Najpierw alokujemy wszystkie brakujace bloki.
     * Jesli zabraknie miejsca, cofamy tylko nowe alokacje i nie zmieniamy
     * rozmiaru ani danych istniejacego pliku.
     */
    uint64_t nowe_id[
        PSF_MAX_BLOKOW_W_WEZLE] = {};

    uint64_t nowe_indeksy[
        PSF_MAX_BLOKOW_W_WEZLE] = {};

    size_t liczba_nowych = 0;

    for (uint64_t k = 0;
         k < potrzebne_bloki;
         ++k) {

        if (w->wskazniki_blokow[k] !=
            BARK_BLOKU) {

            continue;
        }

        const uint64_t nowy =
            zaalokuj_wolny_blok_danych();

        if (nowy ==
            BARK_BLOKU) {

            for (size_t i = 0;
                 i < liczba_nowych;
                 ++i) {

                const uint64_t indeks =
                    nowe_indeksy[i];

                if (indeks <
                    static_cast<uint64_t>(
                        PSF_MAX_BLOKOW_W_WEZLE)) {

                    w->wskazniki_blokow[
                        indeks] =
                        BARK_BLOKU;

                    (void)zwolnij_blok_danych(
                        nowe_id[i]
                    );
                }
            }

            oznacz_wezel_jako_brudny(
                plik_id
            );

            return false;
        }

        w->wskazniki_blokow[k] =
            nowy;

        nowe_indeksy[liczba_nowych] =
            k;

        nowe_id[liczba_nowych] =
            nowy;

        ++liczba_nowych;
    }

    /*
     * Po udanej rezerwacji zapis nie powinien juz wymagac alokacji.
     */
    uint64_t zapisano = 0;

    for (uint64_t k = 0;
         k < potrzebne_bloki;
         ++k) {

        const uint64_t id =
            w->wskazniki_blokow[k];

        uint8_t* blok =
            pobierz_blok_danych(
                id
            );

        if (!blok) {
            /*
             * Metadane byly poprawne przed zapisem; ten przypadek oznacza
             * powazna niespojnosc w trakcie operacji. Nie publikujemy
             * nowego rozmiaru.
             */
            return false;
        }

        uint64_t porcja =
            static_cast<uint64_t>(
                dlugosc) -
            zapisano;

        if (porcja >
            static_cast<uint64_t>(
                PSF_ROZMIAR_BLOKU)) {

            porcja =
                PSF_ROZMIAR_BLOKU;
        }

        kopiuj_pamiec(
            blok,
            dane + zapisano,
            porcja
        );

        /*
         * Zerujemy koniec ostatniego bloku, aby stare dane po skroceniu
         * nie pozostawaly w slack space.
         */
        if (porcja <
            static_cast<uint64_t>(
                PSF_ROZMIAR_BLOKU)) {

            wyzeruj_pamiec(
                blok + porcja,
                static_cast<uint64_t>(
                    PSF_ROZMIAR_BLOKU) -
                    porcja
            );
        }

        const uint64_t absolutny =
            absolutny_blok_danych(
                id
            );

        if (absolutny != UINT64_MAX) {
            oznacz_blok_jako_brudny(
                absolutny
            );
        }

        zapisano += porcja;
    }

    /*
     * Skrocenie pliku zwalnia wszystkie niepotrzebne stare bloki.
     */
    for (uint64_t k = potrzebne_bloki;
         k <
            static_cast<uint64_t>(
                PSF_MAX_BLOKOW_W_WEZLE);
         ++k) {

        const uint64_t id =
            w->wskazniki_blokow[k];

        if (id ==
            BARK_BLOKU) {

            continue;
        }

        if (!zwolnij_blok_danych(
                id)) {

            return false;
        }

        w->wskazniki_blokow[k] =
            BARK_BLOKU;
    }

    w->rozmiar_w_bajtach =
        dlugosc;

    oznacz_wezel_jako_brudny(
        plik_id
    );

    return bsp_zapisz_zmiany();
}

/* =========================================================================
 * PUBLICZNE API - ODCZYT
 * ========================================================================= */

extern "C" bool czytaj_z_pliku(
    const char* sciezka,
    char* bufor,
    uint32_t max_dlugosc
) {
    BlokadaPSF blokada;

    if (!psf_gotowy ||
        !sciezka ||
        (max_dlugosc != 0 &&
         !bufor)) {

        return false;
    }

    const uint64_t plik_id =
        rozwiaz_sciezke(
            sciezka,
            nullptr,
            false
        );

    if (plik_id == 0) {
        return false;
    }

    wezel_indeksowy* w =
        pobierz_wezel(
            plik_id
        );

    if (!w ||
        w->typ != TYP_PLIK ||
        !wskazniki_pliku_poprawne(
            w)) {

        return false;
    }

    uint64_t do_odczytu =
        w->rozmiar_w_bajtach;

    if (do_odczytu >
        static_cast<uint64_t>(
            max_dlugosc)) {

        do_odczytu =
            max_dlugosc;
    }

    uint64_t przeczytano = 0;

    for (uint64_t k = 0;
         przeczytano < do_odczytu;
         ++k) {

        if (k >=
            static_cast<uint64_t>(
                PSF_MAX_BLOKOW_W_WEZLE)) {

            return false;
        }

        const uint64_t id =
            w->wskazniki_blokow[k];

        if (id ==
            BARK_BLOKU) {

            return false;
        }

        uint8_t* blok =
            pobierz_blok_danych(
                id
            );

        if (!blok) {
            return false;
        }

        uint64_t porcja =
            do_odczytu -
            przeczytano;

        if (porcja >
            static_cast<uint64_t>(
                PSF_ROZMIAR_BLOKU)) {

            porcja =
                PSF_ROZMIAR_BLOKU;
        }

        kopiuj_pamiec(
            bufor + przeczytano,
            blok,
            porcja
        );

        przeczytano += porcja;
    }

    return
        przeczytano ==
        do_odczytu;
}

/* =========================================================================
 * PUBLICZNE API - BUFOR DLA LOADERA
 * ========================================================================= */

extern "C" uint8_t* bsp_wczytaj_plik_do_pamieci(
    const char* sciezka,
    uint64_t* rozmiar_wyj
) {
    BlokadaPSF blokada;

    if (!rozmiar_wyj) {
        return nullptr;
    }

    *rozmiar_wyj = 0;

    if (!psf_gotowy ||
        !sciezka) {

        return nullptr;
    }

    const uint64_t plik_id =
        rozwiaz_sciezke(
            sciezka,
            nullptr,
            false
        );

    if (plik_id == 0) {
        return nullptr;
    }

    wezel_indeksowy* w =
        pobierz_wezel(
            plik_id
        );

    if (!w ||
        w->typ != TYP_PLIK ||
        !wskazniki_pliku_poprawne(
            w)) {

        return nullptr;
    }

    const uint64_t dlugosc =
        w->rozmiar_w_bajtach;

    if (dlugosc == 0 ||
        dlugosc >
            MAX_LOADER_BUF) {

        return nullptr;
    }

    uint64_t przeczytano = 0;

    for (uint64_t k = 0;
         przeczytano < dlugosc;
         ++k) {

        if (k >=
            static_cast<uint64_t>(
                PSF_MAX_BLOKOW_W_WEZLE)) {

            *rozmiar_wyj = 0;
            return nullptr;
        }

        const uint64_t id =
            w->wskazniki_blokow[k];

        if (id ==
            BARK_BLOKU) {

            *rozmiar_wyj = 0;
            return nullptr;
        }

        uint8_t* blok =
            pobierz_blok_danych(
                id
            );

        if (!blok) {
            *rozmiar_wyj = 0;
            return nullptr;
        }

        uint64_t porcja =
            dlugosc -
            przeczytano;

        if (porcja >
            static_cast<uint64_t>(
                PSF_ROZMIAR_BLOKU)) {

            porcja =
                PSF_ROZMIAR_BLOKU;
        }

        kopiuj_pamiec(
            bufor_wymiany_plikow +
                przeczytano,
            blok,
            porcja
        );

        przeczytano += porcja;
    }

    if (przeczytano !=
        dlugosc) {

        *rozmiar_wyj = 0;
        return nullptr;
    }

    *rozmiar_wyj =
        dlugosc;

    return
        bufor_wymiany_plikow;
}

/* =========================================================================
 * PUBLICZNE API - LISTOWANIE
 * ========================================================================= */

extern "C" bool wylistuj_katalog(
    const char* sciezka,
    char* bufor,
    uint32_t max_dlugosc
) {
    BlokadaPSF blokada;

    if (!psf_gotowy ||
        !sciezka ||
        !bufor ||
        max_dlugosc == 0) {

        return false;
    }

    const uint64_t katalog_id =
        rozwiaz_sciezke(
            sciezka,
            nullptr,
            false
        );

    if (katalog_id == 0) {
        return false;
    }

    wezel_indeksowy* katalog =
        pobierz_wezel(
            katalog_id
        );

    if (!katalog ||
        katalog->typ != TYP_KATALOG) {

        return false;
    }

    uint32_t pozycja = 0;
    bufor[0] = '\0';

    const uint64_t wpisow_na_blok =
        wpisow_katalogowych_na_blok();

    for (size_t k = 0;
         k <
            static_cast<size_t>(
                PSF_MAX_BLOKOW_W_WEZLE);
         ++k) {

        const uint64_t id_bloku =
            katalog->
                wskazniki_blokow[k];

        if (id_bloku ==
            BARK_BLOKU) {

            continue;
        }

        wpis_katalogowy* wpisy =
            pobierz_wpisy_katalogowe(
                id_bloku
            );

        if (!wpisy) {
            return false;
        }

        for (uint64_t j = 0;
             j < wpisow_na_blok;
             ++j) {

            if (wpisy[j].id_wezla == 0) {
                continue;
            }

            wezel_indeksowy* element =
                pobierz_wezel(
                    wpisy[j].id_wezla
                );

            if (!element ||
                element->typ == TYP_WOLNY ||
                !poprawna_nazwa(
                    wpisy[j].nazwa)) {

                return false;
            }

            const char* tag =
                element->typ ==
                        TYP_KATALOG
                    ? "[KAT]  "
                    : "[PLIK] ";

            for (size_t t = 0;
                 tag[t] != '\0';
                 ++t) {

                if (pozycja + 1 >=
                    max_dlugosc) {

                    bufor[pozycja] =
                        '\0';

                    return true;
                }

                bufor[pozycja++] =
                    tag[t];
            }

            for (size_t n = 0;
                 wpisy[j].nazwa[n] != '\0';
                 ++n) {

                if (n >=
                    static_cast<size_t>(
                        PSF_MAX_NAZWA)) {

                    return false;
                }

                if (pozycja + 1 >=
                    max_dlugosc) {

                    bufor[pozycja] =
                        '\0';

                    return true;
                }

                bufor[pozycja++] =
                    wpisy[j].nazwa[n];
            }

            if (pozycja + 1 <
                max_dlugosc) {

                bufor[pozycja++] =
                    '\n';
            } else {
                bufor[pozycja] =
                    '\0';

                return true;
            }
        }
    }

    bufor[pozycja] = '\0';
    return true;
}

/* =========================================================================
 * PUBLICZNE API - USUWANIE
 * ========================================================================= */

extern "C" bool usun_twor(
    const char* sciezka
) {
    BlokadaPSF blokada;

    if (!psf_gotowy ||
        !bezpieczna_sciezka(
            sciezka)) {

        return false;
    }

    /*
     * Korzenia nie mozna usunac.
     */
    if (sciezka[0] == '/' &&
        sciezka[1] == '\0') {

        return false;
    }

    char nazwa[
        PSF_MAX_NAZWA] = {};

    const uint64_t rodzic_id =
        rozwiaz_sciezke(
            sciezka,
            nazwa,
            true
        );

    if (rodzic_id == 0) {
        return false;
    }

    const ZnalezionyWpis znaleziony =
        znajdz_wpis_w_katalogu(
            rodzic_id,
            nazwa
        );

    if (!znaleziony.znaleziony ||
        znaleziony.id_wezla ==
            dysk_superblok->
                id_korzenia) {

        return false;
    }

    wezel_indeksowy* cel =
        pobierz_wezel(
            znaleziony.id_wezla
        );

    wezel_indeksowy* rodzic =
        pobierz_wezel(
            rodzic_id
        );

    if (!cel ||
        !rodzic ||
        rodzic->typ != TYP_KATALOG ||
        cel->typ == TYP_WOLNY) {

        return false;
    }

    if (cel->typ == TYP_KATALOG &&
        !katalog_pusty(
            znaleziony.id_wezla)) {

        /*
         * Brak rekurencyjnego usuwania. Stara wersja osierocala dzieci.
         */
        return false;
    }

    if (cel->blok_posredni_1 != BARK_BLOKU ||
        cel->blok_posredni_2 != BARK_BLOKU ||
        cel->blok_posredni_3 != BARK_BLOKU) {

        return false;
    }

    /*
     * Wszystkie potencjalne bledy sprawdzamy przed usunieciem wpisu
     * katalogowego.
     */
    for (size_t k = 0;
         k <
            static_cast<size_t>(
                PSF_MAX_BLOKOW_W_WEZLE);
         ++k) {

        const uint64_t id =
            cel->wskazniki_blokow[k];

        if (id ==
            BARK_BLOKU) {

            continue;
        }

        if (!id_bloku_danych_poprawne(id) ||
            !czy_blok_danych_zajety(id)) {

            return false;
        }
    }

    for (size_t k = 0;
         k <
            static_cast<size_t>(
                PSF_MAX_BLOKOW_W_WEZLE);
         ++k) {

        const uint64_t id =
            cel->wskazniki_blokow[k];

        if (id ==
            BARK_BLOKU) {

            continue;
        }

        if (!zwolnij_blok_danych(
                id)) {

            return false;
        }

        cel->wskazniki_blokow[k] =
            BARK_BLOKU;
    }

    wpis_katalogowy* wpisy =
        pobierz_wpisy_katalogowe(
            znaleziony.
                id_bloku_danych
        );

    if (!wpisy ||
        znaleziony.indeks_wpisu >=
            wpisow_katalogowych_na_blok()) {

        return false;
    }

    wpis_katalogowy& wpis =
        wpisy[
            znaleziony.
                indeks_wpisu];

    wpis.id_wezla = 0;

    wyzeruj_pamiec(
        wpis.nazwa,
        PSF_MAX_NAZWA
    );

    const uint64_t absolutny_wpisow =
        absolutny_blok_danych(
            znaleziony.
                id_bloku_danych
        );

    if (absolutny_wpisow !=
        UINT64_MAX) {

        oznacz_blok_jako_brudny(
            absolutny_wpisow
        );
    }

    if (rodzic->
        rozmiar_w_bajtach > 0) {

        rodzic->
            rozmiar_w_bajtach--;
    }

    oznacz_wezel_jako_brudny(
        rodzic_id
    );

    if (!zwolnij_wezel(
            znaleziony.id_wezla)) {

        return false;
    }

    (void)bsp_zapisz_zmiany();

    return true;
}

/* =========================================================================
 * PUBLICZNE API - ROZMIAR
 * ========================================================================= */

extern "C" uint32_t rozmiar_pliku(
    const char* sciezka
) {
    BlokadaPSF blokada;

    if (!psf_gotowy ||
        !sciezka) {

        return 0;
    }

    const uint64_t id =
        rozwiaz_sciezke(
            sciezka,
            nullptr,
            false
        );

    if (id == 0) {
        return 0;
    }

    wezel_indeksowy* w =
        pobierz_wezel(id);

    if (!w ||
        w->typ != TYP_PLIK) {

        return 0;
    }

    if (w->rozmiar_w_bajtach >
        UINT32_MAX) {

        /*
         * Publiczne API ma 32-bitowy wynik.
         */
        return UINT32_MAX;
    }

    return static_cast<uint32_t>(
        w->rozmiar_w_bajtach
    );
}

extern "C" bool czy_katalog_istnieje(const char* sciezka) {
    if (!sciezka)
        return false;

    uint64_t id =
        rozwiaz_sciezke(
            sciezka,
            nullptr,
            false
        );

    if (id == 0)
        return false;

    wezel_indeksowy* w =
        pobierz_wezel(id);

    return
        w != nullptr &&
        w->typ == TYP_KATALOG;
}

extern "C" bool zapewnij_katalog(const char* sciezka) {
    if (czy_katalog_istnieje(sciezka))
        return true;

    return utworz_katalog(sciezka);
}


/* =========================================================================
 * PUBLICZNE API - ZMIANA NAZWY
 * ========================================================================= */

extern "C" bool zmien_nazwe_tworu(
    const char* sciezka,
    const char* nowa_nazwa
) {
    BlokadaPSF blokada;

    if (!psf_gotowy ||
        !bezpieczna_sciezka(
            sciezka) ||
        !poprawna_nazwa(
            nowa_nazwa)) {

        return false;
    }

    if (sciezka[0] == '/' &&
        sciezka[1] == '\0') {

        return false;
    }

    char stara_nazwa[
        PSF_MAX_NAZWA] = {};

    const uint64_t rodzic_id =
        rozwiaz_sciezke(
            sciezka,
            stara_nazwa,
            true
        );

    if (rodzic_id == 0) {
        return false;
    }

    if (nazwy_rowne(
            stara_nazwa,
            nowa_nazwa)) {

        return
            znajdz_wpis_w_katalogu(
                rodzic_id,
                stara_nazwa
            ).znaleziony;
    }

    if (znajdz_wpis_w_katalogu(
            rodzic_id,
            nowa_nazwa).znaleziony) {

        /*
         * Nie nadpisujemy istniejacego wpisu.
         */
        return false;
    }

    const ZnalezionyWpis znaleziony =
        znajdz_wpis_w_katalogu(
            rodzic_id,
            stara_nazwa
        );

    if (!znaleziony.znaleziony) {
        return false;
    }

    wpis_katalogowy* wpisy =
        pobierz_wpisy_katalogowe(
            znaleziony.
                id_bloku_danych
        );

    if (!wpisy ||
        znaleziony.indeks_wpisu >=
            wpisow_katalogowych_na_blok()) {

        return false;
    }

    wpis_katalogowy& wpis =
        wpisy[
            znaleziony.
                indeks_wpisu];

    if (!kopiuj_nazwe(
            wpis.nazwa,
            nowa_nazwa)) {

        return false;
    }

    const uint64_t absolutny =
        absolutny_blok_danych(
            znaleziony.
                id_bloku_danych
        );

    if (absolutny != UINT64_MAX) {
        oznacz_blok_jako_brudny(
            absolutny
        );
    }

    (void)bsp_zapisz_zmiany();

    return true;
}
