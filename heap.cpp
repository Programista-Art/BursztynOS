/*
 * Bursztyn OS - bezpieczniejszy alokator sterty jadra typu first-fit.
 *
 * Wlasciwosci:
 *  - wyrownanie danych do 16 bajtow,
 *  - dzielenie duzych wolnych blokow,
 *  - laczenie sasiadujacych wolnych blokow,
 *  - wykrywanie podstawowych uszkodzen listy sterty,
 *  - ochrona przed integer/pointer overflow,
 *  - odrzucanie obcych wskaznikow i podwojnego free,
 *  - synchronizacja kmalloc/kfree dla wielozadaniowego jadra.
 *
 * Lista blokow jest zawsze przechowywana w kolejnosci adresow i pokrywa
 * caly obszar sterty bez przerw. Dzieki temu mozemy dodatkowo sprawdzac
 * integralnosc metadanych przed wykonaniem alokacji lub zwolnienia.
 */

#include "heap.h"

namespace {

constexpr uint64_t WYROWNANIE = 16ULL;
constexpr uint64_t MINIMALNY_BLOK_UZYTKOWY = 16ULL;
constexpr uint32_t MAGIC_STERTY = 0x12345678U;

/*
 * Prosty spinlock sterty.
 *
 * Przerwania sa lokalnie wylaczane przed przejeciem blokady. Chroni to przed
 * zakleszczeniem na jednym CPU, gdyby handler przerwania probowal wykonac
 * kmalloc/kfree podczas operacji na stercie. Spinlock dodatkowo przygotowuje
 * kod na przyszle SMP.
 */
static uint32_t blokada_sterty = 0;

BlokSterty* pierwszy_blok = nullptr;
uintptr_t poczatek_sterty = 0;
uintptr_t koniec_sterty = 0; // Pierwszy adres juz nienalezacy do sterty.

/* =========================================================================
 * NISKIE FUNKCJE SYNCHRONIZACJI
 * ========================================================================= */

static inline uint64_t zapisz_i_wylacz_przerwania() {
    uint64_t rflags = 0;

    asm volatile(
        "pushfq\n"
        "popq %0\n"
        "cli"
        : "=r"(rflags)
        :
        : "memory");

    return rflags;
}

static inline void przywroc_przerwania(uint64_t rflags) {
    if ((rflags & (1ULL << 9)) != 0) {
        asm volatile("sti" ::: "memory");
    }
}

static inline void przejmij_blokade_sterty() {
    while (__atomic_exchange_n(
               &blokada_sterty,
               1U,
               __ATOMIC_ACQUIRE) != 0U) {
        asm volatile("pause");
    }
}

static inline void zwolnij_blokade_sterty() {
    __atomic_store_n(
        &blokada_sterty,
        0U,
        __ATOMIC_RELEASE);
}

struct BlokadaSterty {
    uint64_t rflags;

    BlokadaSterty()
        : rflags(zapisz_i_wylacz_przerwania()) {
        przejmij_blokade_sterty();
    }

    ~BlokadaSterty() {
        zwolnij_blokade_sterty();
        przywroc_przerwania(rflags);
    }

    BlokadaSterty(const BlokadaSterty&) = delete;
    BlokadaSterty& operator=(const BlokadaSterty&) = delete;
};

/* =========================================================================
 * BEZPIECZNA ARYTMETYKA
 * ========================================================================= */

bool dodaj_uintptr(uintptr_t a,
                   uintptr_t b,
                   uintptr_t& wynik) {
    if (a > UINTPTR_MAX - b) {
        return false;
    }

    wynik = a + b;
    return true;
}

bool dodaj_u64(uint64_t a,
               uint64_t b,
               uint64_t& wynik) {
    if (a > UINT64_MAX - b) {
        return false;
    }

    wynik = a + b;
    return true;
}

bool wyrownaj_rozmiar(uint64_t wartosc,
                      uint64_t& wynik) {
    static_assert(
        (WYROWNANIE & (WYROWNANIE - 1ULL)) == 0,
        "WYROWNANIE musi byc potega dwojki");

    if (wartosc >
        UINT64_MAX - (WYROWNANIE - 1ULL)) {
        return false;
    }

    wynik =
        (wartosc + WYROWNANIE - 1ULL) &
        ~(WYROWNANIE - 1ULL);

    return true;
}

/* =========================================================================
 * WALIDACJA METADANYCH STERTY
 * ========================================================================= */

bool sterta_ma_poprawny_zakres() {
    if (pierwszy_blok == nullptr ||
        poczatek_sterty == 0 ||
        koniec_sterty <= poczatek_sterty) {
        return false;
    }

    if ((poczatek_sterty &
         static_cast<uintptr_t>(WYROWNANIE - 1ULL)) != 0) {
        return false;
    }

    if ((koniec_sterty &
         static_cast<uintptr_t>(WYROWNANIE - 1ULL)) != 0) {
        return false;
    }

    if (reinterpret_cast<uintptr_t>(pierwszy_blok) !=
        poczatek_sterty) {
        return false;
    }

    return true;
}

bool adres_naglowka_w_stercie(const BlokSterty* blok) {
    if (!blok || !sterta_ma_poprawny_zakres()) {
        return false;
    }

    const uintptr_t adres =
        reinterpret_cast<uintptr_t>(blok);

    if ((adres &
         static_cast<uintptr_t>(WYROWNANIE - 1ULL)) != 0) {
        return false;
    }

    if (adres < poczatek_sterty ||
        adres >= koniec_sterty) {
        return false;
    }

    uintptr_t koniec_naglowka = 0;

    if (!dodaj_uintptr(
            adres,
            static_cast<uintptr_t>(sizeof(BlokSterty)),
            koniec_naglowka)) {
        return false;
    }

    return koniec_naglowka <= koniec_sterty;
}

bool pobierz_koniec_bloku(const BlokSterty* blok,
                          uintptr_t& wynik) {
    if (!adres_naglowka_w_stercie(blok)) {
        return false;
    }

    if (blok->magic != MAGIC_STERTY) {
        return false;
    }

    if (blok->rozmiar < MINIMALNY_BLOK_UZYTKOWY) {
        return false;
    }

    if ((blok->rozmiar &
         (WYROWNANIE - 1ULL)) != 0) {
        return false;
    }

    uintptr_t po_naglowku = 0;

    if (!dodaj_uintptr(
            reinterpret_cast<uintptr_t>(blok),
            static_cast<uintptr_t>(sizeof(BlokSterty)),
            po_naglowku)) {
        return false;
    }

    if (blok->rozmiar >
        static_cast<uint64_t>(UINTPTR_MAX - po_naglowku)) {
        return false;
    }

    wynik =
        po_naglowku +
        static_cast<uintptr_t>(blok->rozmiar);

    if (wynik > koniec_sterty ||
        wynik <= po_naglowku) {
        return false;
    }

    return true;
}

bool lista_sterty_poprawna() {
    if (!sterta_ma_poprawny_zakres()) {
        return false;
    }

    const uint64_t rozmiar_sterty =
        static_cast<uint64_t>(
            koniec_sterty - poczatek_sterty);

    /*
     * Teoretyczna maksymalna liczba blokow. Limit zabezpiecza walidator
     * przed nieskonczona petla przy uszkodzeniu wskaznika nastepny.
     */
    const uint64_t minimalny_element =
        static_cast<uint64_t>(sizeof(BlokSterty)) +
        MINIMALNY_BLOK_UZYTKOWY;

    if (minimalny_element == 0) {
        return false;
    }

    const uint64_t maks_blokow =
        (rozmiar_sterty / minimalny_element) + 1ULL;

    BlokSterty* poprzedni = nullptr;
    BlokSterty* blok = pierwszy_blok;
    uint64_t licznik = 0;

    while (blok != nullptr) {
        ++licznik;

        if (licznik > maks_blokow) {
            return false;
        }

        if (!adres_naglowka_w_stercie(blok)) {
            return false;
        }

        if (blok->magic != MAGIC_STERTY) {
            return false;
        }

        if (blok->poprzedni != poprzedni) {
            return false;
        }

        uintptr_t koniec_bloku = 0;

        if (!pobierz_koniec_bloku(
                blok,
                koniec_bloku)) {
            return false;
        }

        if (blok->nastepny != nullptr) {
            /*
             * Lista ma pokrywac sterte bez dziur. Kolejny naglowek musi
             * zaczynac sie dokladnie po danych obecnego bloku.
             */
            if (reinterpret_cast<uintptr_t>(
                    blok->nastepny) != koniec_bloku) {
                return false;
            }

            if (!adres_naglowka_w_stercie(
                    blok->nastepny)) {
                return false;
            }

            if (blok->nastepny->poprzedni != blok) {
                return false;
            }
        } else {
            /*
             * Ostatni blok powinien dochodzic dokladnie do konca sterty.
             */
            if (koniec_bloku != koniec_sterty) {
                return false;
            }
        }

        poprzedni = blok;
        blok = blok->nastepny;
    }

    return licznik != 0;
}

/* =========================================================================
 * OPERACJE NA BLOKACH
 * ========================================================================= */

bool polacz_z_nastepnym(BlokSterty* blok) {
    if (!blok ||
        !adres_naglowka_w_stercie(blok) ||
        blok->magic != MAGIC_STERTY ||
        !blok->czy_wolny) {
        return false;
    }

    BlokSterty* nastepny =
        blok->nastepny;

    if (!nastepny) {
        return false;
    }

    if (!adres_naglowka_w_stercie(nastepny) ||
        nastepny->magic != MAGIC_STERTY ||
        !nastepny->czy_wolny ||
        nastepny->poprzedni != blok) {
        return false;
    }

    uintptr_t koniec_bloku = 0;

    if (!pobierz_koniec_bloku(
            blok,
            koniec_bloku)) {
        return false;
    }

    if (koniec_bloku !=
        reinterpret_cast<uintptr_t>(nastepny)) {
        return false;
    }

    uint64_t nowy_rozmiar = 0;
    uint64_t temp = 0;

    if (!dodaj_u64(
            blok->rozmiar,
            static_cast<uint64_t>(sizeof(BlokSterty)),
            temp)) {
        return false;
    }

    if (!dodaj_u64(
            temp,
            nastepny->rozmiar,
            nowy_rozmiar)) {
        return false;
    }

    /*
     * Sprawdzamy docelowy koniec jeszcze przed modyfikacja listy.
     */
    uintptr_t dane_bloku = 0;

    if (!dodaj_uintptr(
            reinterpret_cast<uintptr_t>(blok),
            static_cast<uintptr_t>(sizeof(BlokSterty)),
            dane_bloku)) {
        return false;
    }

    if (nowy_rozmiar >
        static_cast<uint64_t>(
            UINTPTR_MAX - dane_bloku)) {
        return false;
    }

    const uintptr_t nowy_koniec =
        dane_bloku +
        static_cast<uintptr_t>(nowy_rozmiar);

    if (nowy_koniec > koniec_sterty) {
        return false;
    }

    BlokSterty* po_nastepnym =
        nastepny->nastepny;

    blok->rozmiar = nowy_rozmiar;
    blok->nastepny = po_nastepnym;

    if (po_nastepnym != nullptr) {
        po_nastepnym->poprzedni = blok;
    }

    /*
     * Stary naglowek znajduje sie teraz wewnatrz danych wolnego bloku.
     * Zerujemy wszystkie istotne pola, aby przypadkowy stale pointer
     * nie wygladal jak aktywny naglowek sterty.
     */
    nastepny->rozmiar = 0;
    nastepny->czy_wolny = false;
    nastepny->nastepny = nullptr;
    nastepny->poprzedni = nullptr;
    nastepny->magic = 0;

    return true;
}

BlokSterty* znajdz_blok_po_wskazniku(void* wskaznik) {
    if (!wskaznik ||
        !lista_sterty_poprawna()) {
        return nullptr;
    }

    const uintptr_t szukany =
        reinterpret_cast<uintptr_t>(wskaznik);

    for (BlokSterty* blok = pierwszy_blok;
         blok != nullptr;
         blok = blok->nastepny) {

        uintptr_t adres_danych = 0;

        if (!dodaj_uintptr(
                reinterpret_cast<uintptr_t>(blok),
                static_cast<uintptr_t>(sizeof(BlokSterty)),
                adres_danych)) {
            return nullptr;
        }

        if (adres_danych == szukany) {
            return blok;
        }

        /*
         * Lista jest rosnaca adresowo, wiec po przekroczeniu szukanego
         * adresu nie ma sensu kontynuowac.
         */
        if (adres_danych > szukany) {
            break;
        }
    }

    return nullptr;
}

} // namespace

/* =========================================================================
 * INICJALIZACJA
 * ========================================================================= */

void inicjalizuj_sterte_jadra(
    void* adres_poczatkowy,
    uint64_t rozmiar_poczatkowy) {

    BlokadaSterty blokada;

    /*
     * Nie pozostawiamy aktywnej starej listy po nieudanej
     * reinicjalizacji.
     */
    pierwszy_blok = nullptr;
    poczatek_sterty = 0;
    koniec_sterty = 0;

    if (!adres_poczatkowy ||
        rozmiar_poczatkowy == 0) {
        return;
    }

    const uintptr_t surowy_poczatek =
        reinterpret_cast<uintptr_t>(
            adres_poczatkowy);

    if (rozmiar_poczatkowy >
        static_cast<uint64_t>(
            UINTPTR_MAX - surowy_poczatek)) {
        return;
    }

    const uintptr_t surowy_koniec =
        surowy_poczatek +
        static_cast<uintptr_t>(
            rozmiar_poczatkowy);

    if (surowy_poczatek >
        UINTPTR_MAX -
        static_cast<uintptr_t>(
            WYROWNANIE - 1ULL)) {
        return;
    }

    const uintptr_t wyrownany_poczatek =
        (surowy_poczatek +
         static_cast<uintptr_t>(
             WYROWNANIE - 1ULL)) &
        ~static_cast<uintptr_t>(
            WYROWNANIE - 1ULL);

    const uintptr_t wyrownany_koniec =
        surowy_koniec &
        ~static_cast<uintptr_t>(
            WYROWNANIE - 1ULL);

    if (wyrownany_koniec <=
        wyrownany_poczatek) {
        return;
    }

    const uintptr_t dostepne =
        wyrownany_koniec -
        wyrownany_poczatek;

    const uint64_t minimum =
        static_cast<uint64_t>(
            sizeof(BlokSterty)) +
        MINIMALNY_BLOK_UZYTKOWY;

    if (static_cast<uint64_t>(dostepne) <
        minimum) {
        return;
    }

    poczatek_sterty =
        wyrownany_poczatek;

    koniec_sterty =
        wyrownany_koniec;

    pierwszy_blok =
        reinterpret_cast<BlokSterty*>(
            wyrownany_poczatek);

    pierwszy_blok->rozmiar =
        static_cast<uint64_t>(
            wyrownany_koniec -
            wyrownany_poczatek -
            sizeof(BlokSterty));

    pierwszy_blok->czy_wolny = true;
    pierwszy_blok->nastepny = nullptr;
    pierwszy_blok->poprzedni = nullptr;
    pierwszy_blok->magic = MAGIC_STERTY;

    /*
     * Powinno byc prawdziwe z definicji wyrownan powyzej. Jesli nie jest,
     * dezaktywujemy sterte zamiast pracowac na niespojnym stanie.
     */
    if (!lista_sterty_poprawna()) {
        pierwszy_blok = nullptr;
        poczatek_sterty = 0;
        koniec_sterty = 0;
    }
}

/* =========================================================================
 * KMALLOC
 * ========================================================================= */

void* kmalloc(uint64_t rozmiar) {
    if (rozmiar == 0) {
        return nullptr;
    }

    BlokadaSterty blokada;

    if (!pierwszy_blok ||
        !lista_sterty_poprawna()) {
        return nullptr;
    }

    uint64_t wyrownany_rozmiar = 0;

    if (!wyrownaj_rozmiar(
            rozmiar,
            wyrownany_rozmiar)) {
        return nullptr;
    }

    if (wyrownany_rozmiar <
        MINIMALNY_BLOK_UZYTKOWY) {
        wyrownany_rozmiar =
            MINIMALNY_BLOK_UZYTKOWY;
    }

    for (BlokSterty* blok = pierwszy_blok;
         blok != nullptr;
         blok = blok->nastepny) {

        if (blok->magic != MAGIC_STERTY ||
            !blok->czy_wolny ||
            blok->rozmiar <
                wyrownany_rozmiar) {
            continue;
        }

        const uint64_t pozostalo =
            blok->rozmiar -
            wyrownany_rozmiar;

        /*
         * Dzielimy tylko, gdy pozostala przestrzen pomiesci caly
         * naglowek oraz co najmniej jeden 16-bajtowy blok.
         */
        if (pozostalo >=
            static_cast<uint64_t>(
                sizeof(BlokSterty)) +
            MINIMALNY_BLOK_UZYTKOWY) {

            uintptr_t adres_danych = 0;

            if (!dodaj_uintptr(
                    reinterpret_cast<uintptr_t>(blok),
                    static_cast<uintptr_t>(
                        sizeof(BlokSterty)),
                    adres_danych)) {
                return nullptr;
            }

            if (wyrownany_rozmiar >
                static_cast<uint64_t>(
                    UINTPTR_MAX - adres_danych)) {
                return nullptr;
            }

            const uintptr_t adres_nowego =
                adres_danych +
                static_cast<uintptr_t>(
                    wyrownany_rozmiar);

            if ((adres_nowego &
                 static_cast<uintptr_t>(
                     WYROWNANIE - 1ULL)) != 0) {
                return nullptr;
            }

            uintptr_t koniec_nowego_naglowka = 0;

            if (!dodaj_uintptr(
                    adres_nowego,
                    static_cast<uintptr_t>(
                        sizeof(BlokSterty)),
                    koniec_nowego_naglowka)) {
                return nullptr;
            }

            if (koniec_nowego_naglowka >
                koniec_sterty) {
                return nullptr;
            }

            BlokSterty* nowy =
                reinterpret_cast<BlokSterty*>(
                    adres_nowego);

            BlokSterty* stary_nastepny =
                blok->nastepny;

            nowy->rozmiar =
                pozostalo -
                static_cast<uint64_t>(
                    sizeof(BlokSterty));

            nowy->czy_wolny = true;
            nowy->nastepny = stary_nastepny;
            nowy->poprzedni = blok;
            nowy->magic = MAGIC_STERTY;

            if (stary_nastepny != nullptr) {
                stary_nastepny->poprzedni =
                    nowy;
            }

            blok->rozmiar =
                wyrownany_rozmiar;

            blok->nastepny =
                nowy;
        }

        blok->czy_wolny = false;

        uintptr_t wynik = 0;

        if (!dodaj_uintptr(
                reinterpret_cast<uintptr_t>(blok),
                static_cast<uintptr_t>(
                    sizeof(BlokSterty)),
                wynik)) {
            /*
             * Teoretycznie nieosiagalne po walidacji listy.
             * Cofamy znacznik zajetosci, aby nie zgubic bloku.
             */
            blok->czy_wolny = true;
            return nullptr;
        }

        return reinterpret_cast<void*>(wynik);
    }

    return nullptr;
}

/* =========================================================================
 * KFREE
 * ========================================================================= */

void kfree(void* wskaznik) {
    if (!wskaznik) {
        return;
    }

    BlokadaSterty blokada;

    if (!pierwszy_blok ||
        !lista_sterty_poprawna()) {
        return;
    }

    const uintptr_t adres_uzytkowy =
        reinterpret_cast<uintptr_t>(
            wskaznik);

    uintptr_t minimalny_adres_danych = 0;

    if (!dodaj_uintptr(
            poczatek_sterty,
            static_cast<uintptr_t>(
                sizeof(BlokSterty)),
            minimalny_adres_danych)) {
        return;
    }

    if (adres_uzytkowy <
            minimalny_adres_danych ||
        adres_uzytkowy >=
            koniec_sterty ||
        (adres_uzytkowy &
         static_cast<uintptr_t>(
             WYROWNANIE - 1ULL)) != 0) {
        return;
    }

    /*
     * Nie wyliczamy po prostu ptr - sizeof(header) i nie ufamy przypadkowej
     * wartosci magic. Szukamy dokladnego poczatku danych na prawidlowej
     * liscie. Chroni to przed kfree(ptr + offset).
     */
    BlokSterty* blok =
        znajdz_blok_po_wskazniku(
            wskaznik);

    if (!blok ||
        blok->magic != MAGIC_STERTY ||
        blok->czy_wolny) {
        return;
    }

    blok->czy_wolny = true;

    /*
     * Najpierw wchlaniamy prawy blok.
     */
    (void)polacz_z_nastepnym(blok);

    /*
     * Potem, jesli lewy blok jest wolny, laczymy cala nowa przestrzen
     * do niego. Przed dereferencja poprzedniego sprawdzamy jego adres.
     */
    BlokSterty* poprzedni =
        blok->poprzedni;

    if (poprzedni != nullptr &&
        adres_naglowka_w_stercie(
            poprzedni) &&
        poprzedni->magic ==
            MAGIC_STERTY &&
        poprzedni->czy_wolny) {

        (void)polacz_z_nastepnym(
            poprzedni);
    }

    /*
     * Po operacji integralnosc powinna nadal byc zachowana.
     * Nie panikujemy tutaj, bo diagnostyka/panic moze sama korzystac
     * ze sterty. Kolejne kmalloc/kfree i tak odmowia pracy, jesli
     * walidacja wykryje uszkodzenie.
     */
    (void)lista_sterty_poprawna();
}

/* =========================================================================
 * GLOBALNE OPERATORY C++
 * ========================================================================= */

void* operator new(size_t size) {
    /*
     * C++ dopuszcza new T[0]; operator new powinien wtedy otrzymac szanse
     * zwrocenia unikalnego wskaznika zamiast automatycznego nullptr.
     */
    if (size == 0) {
        size = 1;
    }

    return kmalloc(
        static_cast<uint64_t>(size));
}

void* operator new[](size_t size) {
    if (size == 0) {
        size = 1;
    }

    return kmalloc(
        static_cast<uint64_t>(size));
}

void operator delete(void* p) noexcept {
    kfree(p);
}

void operator delete[](void* p) noexcept {
    kfree(p);
}

void operator delete(void* p,
                     size_t) noexcept {
    kfree(p);
}

void operator delete[](void* p,
                       size_t) noexcept {
    kfree(p);
}
