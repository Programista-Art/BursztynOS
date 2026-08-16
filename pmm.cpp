/*
 * Bursztyn OS - Physical Memory Manager (PMM)
 *
 * Bitmapowy alokator fizycznych ramek 4 KiB.
 *
 * Wlasciwosci:
 *  - poprawne parsowanie struktury informacji Multiboot2,
 *  - uwalnianie tylko pelnych ramek nalezacych do MEMORY_AVAILABLE,
 *  - stale zabezpieczenie obrazu jadra, Multiboot2, modulow i framebufferu,
 *  - ochrona przed przydzieleniem MMIO LAPIC/IOAPIC,
 *  - ochrona przed double-free i zwalnianiem ramek spoza RAM,
 *  - next-fit zamiast skanowania zawsze od ramki 0,
 *  - synchronizacja PMM spinlockiem + lokalnym wylaczeniem IRQ,
 *  - brak zgadywania ilosci RAM, gdy mapa Multiboot2 jest uszkodzona.
 *
 * UWAGA O OBECNEJ ARCHITEKTURZE:
 *
 * boot.S tworzy poczatkowo identity mapping tylko dla 0..1 GiB.
 * Jednoczesnie wiele obecnych modulow Bursztyna nadal dereferencjonuje
 * wartosc zwrocona przez ZaalokujRamke() jak zwykly wskaznik.
 *
 * Dlatego PMM sledzi pamiec fizyczna do 4 GiB, ale aktualnie WYDAJE tylko
 * ramki ponizej 1 GiB. Po wdrozeniu stalego direct-map/HHDM w VMM ten limit
 * mozna bezpiecznie podniesc.
 */

#include "pamiec.h"

#include <stdint.h>
#include <stddef.h>

/* =========================================================================
 * STALE
 * ========================================================================= */

namespace {

constexpr uint64_t ROZMIAR_STRONY =
    static_cast<uint64_t>(ROZMIAR_RAMKI);

/*
 * PMM sledzi dokladnie pierwsze 4 GiB przestrzeni fizycznej.
 */
constexpr uint64_t MAKS_ADRES_FIZYCZNY_PMM =
    0x0000000100000000ULL;

constexpr uint64_t MAX_RAMEK =
    MAKS_ADRES_FIZYCZNY_PMM / ROZMIAR_STRONY;

constexpr uint64_t ROZMIAR_MAPY_BITOWEJ =
    MAX_RAMEK / 8ULL;

/*
 * boot.S:
 *   512 wpisow PDE * 2 MiB = 1 GiB identity mapping.
 *
 * Dopoki VMM nie udostepnia gwarantowanego direct-map calego RAM,
 * fizyczne wskazniki zwracane przez PMM musza miescic sie w tym obszarze.
 */
constexpr uint64_t MAKS_ADRES_ALOKOWALNY =
    0x0000000040000000ULL;

constexpr uint64_t MAX_RAMEK_ALOKOWALNYCH =
    MAKS_ADRES_ALOKOWALNY / ROZMIAR_STRONY;

constexpr uint64_t GRANICA_NISKIEJ_PAMIECI =
    0x00100000ULL;

/*
 * Standardowe adresy xAPIC/IOAPIC dla obecnego PC/QEMU.
 * ACPI MADT powinno docelowo zastapic hardkodowany adres IOAPIC.
 */
constexpr uint64_t LAPIC_MMIO =
    0xFEE00000ULL;

constexpr uint64_t IOAPIC_MMIO =
    0xFEC00000ULL;

constexpr uint32_t MULTIBOOT_TAG_TYPE_MODULE =
    3U;

constexpr uint32_t MULTIBOOT_MIN_TOTAL_SIZE =
    16U;

constexpr uint32_t MULTIBOOT_TAG_HEADER_SIZE =
    8U;

constexpr uint32_t MULTIBOOT_MMAP_HEADER_SIZE =
    16U;

constexpr uint32_t MULTIBOOT_MMAP_ENTRY_MIN_SIZE =
    sizeof(WpisMapyPamieciMB2);

/* =========================================================================
 * MAPY BITOWE
 * ========================================================================= */

/*
 * mapa_bitowa:
 *   1 = ramka jest aktualnie wolna
 *   0 = ramka jest zajeta / niedostepna
 *
 * Nazwe zachowujemy dla kompatybilnosci z dotychczasowym debugowaniem.
 */
alignas(64)
uint8_t mapa_bitowa[ROZMIAR_MAPY_BITOWEJ] = {};

/*
 * mapa_dostepnosci:
 *   1 = ramka nalezy do poprawnego regionu MEMORY_AVAILABLE i moze byc
 *       w przyszlosci zwolniona,
 *   0 = ramka nigdy nie moze trafic do zwyklego alokatora.
 *
 * Dzieki oddzielnej mapie ZwolnijRamke() nie moze przypadkiem "odblokowac"
 * kernela, framebufferu, MMIO albo nieistniejacej pamieci.
 */
alignas(64)
uint8_t mapa_dostepnosci[ROZMIAR_MAPY_BITOWEJ] = {};

uint64_t ostatnia_alokacja = 0;

bool pmm_zainicjalizowany = false;

/*
 * Blokada jest potrzebna po uruchomieniu schedulera. Lokalny CLI dodatkowo
 * zabezpiecza jeden CPU przed przerwaniem w srodku operacji PMM.
 */
uint32_t blokada_pmm = 0;

/* =========================================================================
 * STRUKTURY MULTIBOOT2 NIEOBECNE W PUBLICZNYM NAGLOWKU
 * ========================================================================= */

struct TagModuluMB2 {
    uint32_t typ;
    uint32_t rozmiar;
    uint32_t mod_start;
    uint32_t mod_end;
} __attribute__((packed));

static_assert(
    sizeof(TagModuluMB2) == 16,
    "Stala czesc tagu modulu Multiboot2 musi miec 16 bajtow"
);

static_assert(
    offsetof(TagModuluMB2, mod_start) == 8,
    "Nieprawidlowy layout tagu modulu Multiboot2"
);

static_assert(
    offsetof(TagModuluMB2, mod_end) == 12,
    "Nieprawidlowy layout tagu modulu Multiboot2"
);

/* =========================================================================
 * SYNCHRONIZACJA
 * ========================================================================= */

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

static void zablokuj_pmm() {
    while (__atomic_exchange_n(
               &blokada_pmm,
               1U,
               __ATOMIC_ACQUIRE) != 0U) {

        while (__atomic_load_n(
                   &blokada_pmm,
                   __ATOMIC_RELAXED) != 0U) {
            asm volatile("pause");
        }
    }
}

static void odblokuj_pmm() {
    __atomic_store_n(
        &blokada_pmm,
        0U,
        __ATOMIC_RELEASE
    );
}

class BlokadaPMM {
public:
    BlokadaPMM()
        : stan_(wylacz_przerwania()) {
        zablokuj_pmm();
    }

    ~BlokadaPMM() {
        odblokuj_pmm();
        przywroc_przerwania(stan_);
    }

    BlokadaPMM(const BlokadaPMM&) = delete;
    BlokadaPMM& operator=(const BlokadaPMM&) = delete;

private:
    StanPrzerwan stan_;
};

/* =========================================================================
 * PODSTAWOWE OPERACJE BITOWE - WYMAGAJA POSIADANIA BLOKADY PMM
 * ========================================================================= */

static inline bool numer_ramki_poprawny(
    uint64_t numer_ramki
) {
    return numer_ramki < MAX_RAMEK;
}

static inline uint64_t indeks_bajtu(
    uint64_t numer_ramki
) {
    return numer_ramki >> 3;
}

static inline uint8_t maska_bitu(
    uint64_t numer_ramki
) {
    return static_cast<uint8_t>(
        1U << static_cast<uint8_t>(
            numer_ramki & 7ULL)
    );
}

static inline bool bit_ustawiony(
    const uint8_t* mapa,
    uint64_t numer_ramki
) {
    if (!mapa || !numer_ramki_poprawny(numer_ramki)) {
        return false;
    }

    return
        (mapa[indeks_bajtu(numer_ramki)] &
         maska_bitu(numer_ramki)) != 0;
}

static inline void ustaw_bit(
    uint8_t* mapa,
    uint64_t numer_ramki
) {
    if (!mapa || !numer_ramki_poprawny(numer_ramki)) {
        return;
    }

    mapa[indeks_bajtu(numer_ramki)] |=
        maska_bitu(numer_ramki);
}

static inline void wyczysc_bit(
    uint8_t* mapa,
    uint64_t numer_ramki
) {
    if (!mapa || !numer_ramki_poprawny(numer_ramki)) {
        return;
    }

    mapa[indeks_bajtu(numer_ramki)] &=
        static_cast<uint8_t>(
            ~maska_bitu(numer_ramki)
        );
}

static inline bool ramka_dostepna(
    uint64_t numer_ramki
) {
    return bit_ustawiony(
        mapa_dostepnosci,
        numer_ramki
    );
}

static inline bool ramka_wolna(
    uint64_t numer_ramki
) {
    return bit_ustawiony(
        mapa_bitowa,
        numer_ramki
    );
}

static inline void ustaw_ramke_wolna(
    uint64_t numer_ramki
) {
    if (!ramka_dostepna(numer_ramki)) {
        return;
    }

    ustaw_bit(
        mapa_bitowa,
        numer_ramki
    );
}

static inline void ustaw_ramke_zajeta(
    uint64_t numer_ramki
) {
    wyczysc_bit(
        mapa_bitowa,
        numer_ramki
    );
}

static inline void oznacz_ramke_jako_ram(
    uint64_t numer_ramki
) {
    if (!numer_ramki_poprawny(numer_ramki)) {
        return;
    }

    ustaw_bit(
        mapa_dostepnosci,
        numer_ramki
    );

    ustaw_bit(
        mapa_bitowa,
        numer_ramki
    );
}

static inline void zarezerwuj_ramke_na_stale(
    uint64_t numer_ramki
) {
    if (!numer_ramki_poprawny(numer_ramki)) {
        return;
    }

    /*
     * Usuniecie bitu dostepnosci powoduje, ze pozniejsze przypadkowe
     * ZwolnijRamke() nie przywroci tej ramki do puli.
     */
    wyczysc_bit(
        mapa_dostepnosci,
        numer_ramki
    );

    wyczysc_bit(
        mapa_bitowa,
        numer_ramki
    );
}

/* =========================================================================
 * ARYTMETYKA I WYROWNANIE
 * ========================================================================= */

static bool dodaj_u64(
    uint64_t a,
    uint64_t b,
    uint64_t* wynik
) {
    if (!wynik) return false;

    if (a > UINT64_MAX - b) {
        return false;
    }

    *wynik = a + b;
    return true;
}

static bool wyrownaj_w_gore_do_strony(
    uint64_t wartosc,
    uint64_t* wynik
) {
    if (!wynik) return false;

    if ((wartosc & (ROZMIAR_STRONY - 1ULL)) == 0) {
        *wynik = wartosc;
        return true;
    }

    const uint64_t dodatek =
        ROZMIAR_STRONY -
        (wartosc & (ROZMIAR_STRONY - 1ULL));

    return dodaj_u64(
        wartosc,
        dodatek,
        wynik
    );
}

static inline uint64_t wyrownaj_w_dol_do_strony(
    uint64_t wartosc
) {
    return
        wartosc &
        ~(ROZMIAR_STRONY - 1ULL);
}

static bool wyrownaj_w_gore_do_8(
    uint64_t wartosc,
    uint64_t* wynik
) {
    if (!wynik) return false;

    if (wartosc >
        UINT64_MAX -
        MULTIBOOT2_MASKA_WYROWNANIA) {
        return false;
    }

    *wynik =
        (wartosc +
         MULTIBOOT2_MASKA_WYROWNANIA) &
        ~MULTIBOOT2_MASKA_WYROWNANIA;

    return true;
}

/* =========================================================================
 * BEZPIECZNE KOPIOWANIE MALYCH STRUKTUR
 * ========================================================================= */

static void kopiuj_bajty(
    void* cel,
    const void* zrodlo,
    size_t rozmiar
) {
    if (!cel || !zrodlo) return;

    uint8_t* dst =
        static_cast<uint8_t*>(cel);

    const uint8_t* src =
        static_cast<const uint8_t*>(zrodlo);

    for (size_t i = 0; i < rozmiar; ++i) {
        dst[i] = src[i];
    }
}

/* =========================================================================
 * OPERACJE NA ZAKRESACH - WYMAGAJA POSIADANIA BLOKADY PMM
 * ========================================================================= */

/*
 * Oznacza jako zwykly RAM tylko PELNE strony calkowicie zawarte w regionie
 * MEMORY_AVAILABLE.
 */
static void dodaj_dostepny_zakres(
    uint64_t poczatek,
    uint64_t dlugosc
) {
    if (dlugosc == 0) return;
    if (poczatek >= MAKS_ADRES_FIZYCZNY_PMM) return;

    uint64_t koniec = 0;

    if (dlugosc >
        MAKS_ADRES_FIZYCZNY_PMM - poczatek) {
        koniec = MAKS_ADRES_FIZYCZNY_PMM;
    } else {
        koniec = poczatek + dlugosc;
    }

    uint64_t start_wyrownany = 0;

    if (!wyrownaj_w_gore_do_strony(
            poczatek,
            &start_wyrownany)) {
        return;
    }

    const uint64_t koniec_wyrownany =
        wyrownaj_w_dol_do_strony(
            koniec
        );

    if (start_wyrownany >= koniec_wyrownany) {
        return;
    }

    const uint64_t pierwsza_ramka =
        start_wyrownany / ROZMIAR_STRONY;

    const uint64_t ramka_za_koncem =
        koniec_wyrownany / ROZMIAR_STRONY;

    for (uint64_t ramka = pierwsza_ramka;
         ramka < ramka_za_koncem;
         ++ramka) {

        oznacz_ramke_jako_ram(ramka);
    }

    if (ramka_za_koncem >
        najwyzsza_znaleziona_ramka) {

        najwyzsza_znaleziona_ramka =
            ramka_za_koncem;
    }
}

/*
 * Rezerwacja stalego obszaru zajmuje kazda strone, ktora choc czesciowo
 * przecina wskazany zakres.
 */
static void zarezerwuj_zakres_na_stale(
    uint64_t poczatek,
    uint64_t dlugosc
) {
    if (dlugosc == 0) return;
    if (poczatek >= MAKS_ADRES_FIZYCZNY_PMM) return;

    uint64_t koniec = 0;

    if (dlugosc >
        MAKS_ADRES_FIZYCZNY_PMM - poczatek) {
        koniec = MAKS_ADRES_FIZYCZNY_PMM;
    } else {
        koniec = poczatek + dlugosc;
    }

    const uint64_t start_wyrownany =
        wyrownaj_w_dol_do_strony(
            poczatek
        );

    uint64_t koniec_wyrownany = 0;

    if (!wyrownaj_w_gore_do_strony(
            koniec,
            &koniec_wyrownany)) {

        koniec_wyrownany =
            MAKS_ADRES_FIZYCZNY_PMM;
    }

    if (koniec_wyrownany >
        MAKS_ADRES_FIZYCZNY_PMM) {

        koniec_wyrownany =
            MAKS_ADRES_FIZYCZNY_PMM;
    }

    if (start_wyrownany >= koniec_wyrownany) {
        return;
    }

    const uint64_t pierwsza_ramka =
        start_wyrownany / ROZMIAR_STRONY;

    const uint64_t ramka_za_koncem =
        koniec_wyrownany / ROZMIAR_STRONY;

    for (uint64_t ramka = pierwsza_ramka;
         ramka < ramka_za_koncem;
         ++ramka) {

        zarezerwuj_ramke_na_stale(
            ramka
        );
    }
}

/* =========================================================================
 * WALIDACJA STRUKTURY INFORMACJI MULTIBOOT2
 * ========================================================================= */

struct ZakresMultiboot {
    uint64_t poczatek;
    uint64_t koniec;
};

static bool pobierz_zakres_multiboot(
    uint64_t adres_info_multiboot,
    ZakresMultiboot* wynik
) {
    if (!wynik) return false;
    if (adres_info_multiboot == 0) return false;

    /*
     * Multiboot2 wymaga 8-bajtowego wyrownania adresu struktury.
     */
    if ((adres_info_multiboot &
         (MULTIBOOT2_WYROWNANIE_TAGU - 1ULL)) != 0) {
        return false;
    }

    const uint32_t calkowity_rozmiar =
        *reinterpret_cast<const uint32_t*>(
            adres_info_multiboot
        );

    if (calkowity_rozmiar <
        MULTIBOOT_MIN_TOTAL_SIZE) {
        return false;
    }

    uint64_t koniec = 0;

    if (!dodaj_u64(
            adres_info_multiboot,
            static_cast<uint64_t>(
                calkowity_rozmiar),
            &koniec)) {
        return false;
    }

    if (koniec <= adres_info_multiboot + 8ULL) {
        return false;
    }

    wynik->poczatek =
        adres_info_multiboot;

    wynik->koniec =
        koniec;

    return true;
}

template <typename Funkcja>
static bool przejdz_po_tagach_multiboot(
    const ZakresMultiboot& mb,
    Funkcja funkcja
) {
    uint64_t aktualny =
        mb.poczatek + 8ULL;

    bool znaleziono_koniec = false;

    while (aktualny < mb.koniec) {
        if (mb.koniec - aktualny <
            MULTIBOOT_TAG_HEADER_SIZE) {
            return false;
        }

        const WpisTaguMB2* tag =
            reinterpret_cast<const WpisTaguMB2*>(
                aktualny
            );

        const uint32_t rozmiar =
            tag->rozmiar;

        if (rozmiar <
            MULTIBOOT_TAG_HEADER_SIZE) {
            return false;
        }

        uint64_t koniec_tagu = 0;

        if (!dodaj_u64(
                aktualny,
                static_cast<uint64_t>(rozmiar),
                &koniec_tagu)) {
            return false;
        }

        if (koniec_tagu > mb.koniec) {
            return false;
        }

        if (tag->typ == MULTIBOOT_TAG_TYPE_END) {
            if (rozmiar != 8U) {
                return false;
            }

            znaleziono_koniec = true;
            break;
        }

        if (!funkcja(tag)) {
            return false;
        }

        uint64_t krok = 0;

        if (!wyrownaj_w_gore_do_8(
                static_cast<uint64_t>(rozmiar),
                &krok)) {
            return false;
        }

        if (krok == 0 ||
            krok > mb.koniec - aktualny) {
            return false;
        }

        aktualny += krok;
    }

    return znaleziono_koniec;
}

/* =========================================================================
 * PARSOWANIE MAPY PAMIECI
 * ========================================================================= */

static bool przetworz_mape_pamieci(
    const WpisTaguMB2* zwykly_tag
) {
    if (!zwykly_tag) return false;

    if (zwykly_tag->rozmiar <
        MULTIBOOT_MMAP_HEADER_SIZE) {
        return false;
    }

    const TagMapyPamieciMB2* mapa =
        reinterpret_cast<const TagMapyPamieciMB2*>(
            zwykly_tag
        );

    if (mapa->rozmiar_wpisu <
        MULTIBOOT_MMAP_ENTRY_MIN_SIZE) {
        return false;
    }

    const uint64_t payload =
        static_cast<uint64_t>(
            mapa->rozmiar -
            MULTIBOOT_MMAP_HEADER_SIZE
        );

    if (payload == 0) {
        return false;
    }

    /*
     * Dodatkowe bajty w przyszlym formacie wpisu sa dozwolone,
     * ale liczba bajtow calego payloadu musi byc wielokrotnoscia
     * entry_size.
     */
    if ((payload %
         static_cast<uint64_t>(
             mapa->rozmiar_wpisu)) != 0) {
        return false;
    }

    const uint64_t liczba_wpisow =
        payload /
        static_cast<uint64_t>(
            mapa->rozmiar_wpisu);

    const uint8_t* baza_wpisow =
        reinterpret_cast<const uint8_t*>(
            zwykly_tag
        ) +
        MULTIBOOT_MMAP_HEADER_SIZE;

    for (uint64_t i = 0;
         i < liczba_wpisow;
         ++i) {

        const uint64_t offset =
            i *
            static_cast<uint64_t>(
                mapa->rozmiar_wpisu);

        WpisMapyPamieciMB2 wpis{};

        kopiuj_bajty(
            &wpis,
            baza_wpisow + offset,
            sizeof(wpis)
        );

        if (wpis.typ_obszaru !=
            MULTIBOOT_MEMORY_AVAILABLE) {
            continue;
        }

        dodaj_dostepny_zakres(
            wpis.adres_bazowy,
            wpis.dlugosc
        );
    }

    return true;
}

/* =========================================================================
 * REZERWACJA OBIEKTOW OPISANYCH PRZEZ MULTIBOOT2
 * ========================================================================= */

static void zarezerwuj_modul(
    const WpisTaguMB2* zwykly_tag
) {
    if (!zwykly_tag) return;

    if (zwykly_tag->rozmiar < 16U) {
        return;
    }

    TagModuluMB2 modul{};

    /*
     * Kopiujemy tylko stala czesc tagu. Pole tekst jest zmiennej dlugosci.
     */
    kopiuj_bajty(
        &modul,
        zwykly_tag,
        16U
    );

    const uint64_t start =
        static_cast<uint64_t>(
            modul.mod_start);

    const uint64_t koniec =
        static_cast<uint64_t>(
            modul.mod_end);

    if (koniec <= start) {
        return;
    }

    zarezerwuj_zakres_na_stale(
        start,
        koniec - start
    );
}

static void zarezerwuj_framebuffer(
    const WpisTaguMB2* zwykly_tag
) {
    if (!zwykly_tag) return;

    if (zwykly_tag->rozmiar <
        sizeof(TagFramebufferMB2)) {
        return;
    }

    TagFramebufferMB2 fb{};

    kopiuj_bajty(
        &fb,
        zwykly_tag,
        sizeof(fb)
    );

    if (fb.adres_fizyczny == 0 ||
        fb.pitch == 0 ||
        fb.wysokosc == 0) {
        return;
    }

    const uint64_t pitch =
        static_cast<uint64_t>(
            fb.pitch);

    const uint64_t wysokosc =
        static_cast<uint64_t>(
            fb.wysokosc);

    if (wysokosc >
        UINT64_MAX / pitch) {
        return;
    }

    const uint64_t rozmiar =
        pitch * wysokosc;

    zarezerwuj_zakres_na_stale(
        fb.adres_fizyczny,
        rozmiar
    );
}

/* =========================================================================
 * RESET STANU PMM
 * ========================================================================= */

static void wyzeruj_stan_pmm() {
    for (uint64_t i = 0;
         i < ROZMIAR_MAPY_BITOWEJ;
         ++i) {

        mapa_bitowa[i] = 0;
        mapa_dostepnosci[i] = 0;
    }

    najwyzsza_znaleziona_ramka = 0;
    ostatnia_alokacja = 0;
    pmm_zainicjalizowany = false;
}

/* =========================================================================
 * PUBLICZNE OPERACJE NA POJEDYNCZEJ RAMCE
 * ========================================================================= */

static bool adres_na_numer_ramki(
    uint64_t adres_fizyczny,
    uint64_t* numer_ramki
) {
    if (!numer_ramki) return false;

    if (adres_fizyczny >=
        MAKS_ADRES_FIZYCZNY_PMM) {
        return false;
    }

    *numer_ramki =
        adres_fizyczny /
        ROZMIAR_STRONY;

    return numer_ramki_poprawny(
        *numer_ramki
    );
}

} // namespace

/* =========================================================================
 * ZMIENNE PUBLICZNE PMM
 * ========================================================================= */

uint64_t najwyzsza_znaleziona_ramka = 0;

/* =========================================================================
 * API PMM
 * ========================================================================= */

void OdblokujRamke(
    uint64_t adres_fizyczny
) {
    BlokadaPMM blokada;

    if (!pmm_zainicjalizowany) {
        return;
    }

    uint64_t numer_ramki = 0;

    if (!adres_na_numer_ramki(
            adres_fizyczny,
            &numer_ramki)) {
        return;
    }

    /*
     * Nie zmieniamy bitu mapa_dostepnosci.
     * Ramka stale zarezerwowana nie moze zostac "ożywiona" tym API.
     */
    ustaw_ramke_wolna(
        numer_ramki
    );

    if (numer_ramki <
        ostatnia_alokacja) {
        ostatnia_alokacja =
            numer_ramki;
    }
}

void ZabezpieczRamke(
    uint64_t adres_fizyczny
) {
    BlokadaPMM blokada;

    if (!pmm_zainicjalizowany) {
        return;
    }

    uint64_t numer_ramki = 0;

    if (!adres_na_numer_ramki(
            adres_fizyczny,
            &numer_ramki)) {
        return;
    }

    ustaw_ramke_zajeta(
        numer_ramki
    );
}

bool CzyRamkaWolna(
    uint64_t adres_fizyczny
) {
    BlokadaPMM blokada;

    if (!pmm_zainicjalizowany) {
        return false;
    }

    uint64_t numer_ramki = 0;

    if (!adres_na_numer_ramki(
            adres_fizyczny,
            &numer_ramki)) {
        return false;
    }

    return
        ramka_dostepna(numer_ramki) &&
        ramka_wolna(numer_ramki);
}

/* =========================================================================
 * INICJALIZACJA
 * ========================================================================= */

void InicjalizujPMM(
    uint64_t adres_info_multiboot
) {
    BlokadaPMM blokada;

    wyzeruj_stan_pmm();

    ZakresMultiboot mb{};

    if (!pobierz_zakres_multiboot(
            adres_info_multiboot,
            &mb)) {
        /*
         * Fail closed. Nie zgadujemy "512 MB", poniewaz prowadzi to
         * do alokacji nieistniejacej albo zarezerwowanej pamieci.
         */
        return;
    }

    bool znaleziono_mape = false;
    bool mapa_poprawna = true;

    /*
     * Etap 1:
     * Udostepniamy tylko regiony jawnie oznaczone MEMORY_AVAILABLE.
     */
    const bool tagi_poprawne =
        przejdz_po_tagach_multiboot(
            mb,
            [&](const WpisTaguMB2* tag) -> bool {
                if (tag->typ ==
                    MULTIBOOT_TAG_TYPE_MEMORY_MAP) {

                    /*
                     * Dwie mapy pamieci sa nietypowe i niepotrzebnie
                     * komplikowalyby priorytety. Traktujemy to jako blad.
                     */
                    if (znaleziono_mape) {
                        mapa_poprawna = false;
                        return false;
                    }

                    znaleziono_mape = true;

                    if (!przetworz_mape_pamieci(tag)) {
                        mapa_poprawna = false;
                        return false;
                    }
                }

                return true;
            }
        );

    if (!tagi_poprawne ||
        !znaleziono_mape ||
        !mapa_poprawna ||
        najwyzsza_znaleziona_ramka == 0) {

        wyzeruj_stan_pmm();
        return;
    }

    /*
     * Etap 2:
     * Rezerwujemy obszary, ktore moga lezec wewnatrz regionu oznaczonego
     * przez firmware jako AVAILABLE.
     */

    /*
     * Pierwszy 1 MiB:
     * IVT/BDA, VGA, BIOS, legacy MMIO oraz adres 0.
     *
     * Rezerwacja adresu 0 jest szczegolnie wazna, poniewaz nullptr jest
     * sygnalem bledu funkcji ZaalokujRamke().
     */
    zarezerwuj_zakres_na_stale(
        0,
        GRANICA_NISKIEJ_PAMIECI
    );

    /*
     * Caly obraz jadra, wlacznie z .boot, .text, .rodata, .data, .bss,
     * tablicami startowymi i stosem z boot.S.
     */
    const uint64_t jadro_start =
        reinterpret_cast<uint64_t>(
            __kernel_start
        );

    const uint64_t jadro_koniec =
        reinterpret_cast<uint64_t>(
            __kernel_end
        );

    if (jadro_koniec <= jadro_start) {
        wyzeruj_stan_pmm();
        return;
    }

    zarezerwuj_zakres_na_stale(
        jadro_start,
        jadro_koniec - jadro_start
    );

    /*
     * Sama struktura informacji Multiboot2 musi pozostac zywa co najmniej
     * do zakonczenia inicjalizacji wszystkich modulow czytajacych jej tagi.
     * PMM oznacza ja stale zajeta - jej rozmiar jest maly, a upraszcza to
     * bezpieczenstwo wczesnego startu.
     */
    zarezerwuj_zakres_na_stale(
        mb.poczatek,
        mb.koniec - mb.poczatek
    );

    /*
     * Etap 3:
     * Rezerwujemy moduly GRUB i framebuffer opisane w tagach.
     */
    const bool rezerwacje_ok =
        przejdz_po_tagach_multiboot(
            mb,
            [&](const WpisTaguMB2* tag) -> bool {
                if (tag->typ ==
                    MULTIBOOT_TAG_TYPE_MODULE) {

                    zarezerwuj_modul(tag);
                } else if (tag->typ ==
                           MULTIBOOT_TAG_TYPE_FRAMEBUFFER) {

                    zarezerwuj_framebuffer(tag);
                }

                return true;
            }
        );

    if (!rezerwacje_ok) {
        wyzeruj_stan_pmm();
        return;
    }

    /*
     * Nawet jesli firmware przypadkiem oznaczy MMIO jako RAM, PMM nigdy
     * nie wyda tych stron jako zwyklej pamieci.
     */
    zarezerwuj_zakres_na_stale(
        LAPIC_MMIO,
        ROZMIAR_STRONY
    );

    zarezerwuj_zakres_na_stale(
        IOAPIC_MMIO,
        ROZMIAR_STRONY
    );

    /*
     * Kursor zaczyna od pierwszej ramki powyzej niskiego 1 MiB.
     */
    ostatnia_alokacja =
        GRANICA_NISKIEJ_PAMIECI /
        ROZMIAR_STRONY;

    pmm_zainicjalizowany = true;
}

/* =========================================================================
 * ALOKACJA - NEXT FIT
 * ========================================================================= */

void* ZaalokujRamke() {
    BlokadaPMM blokada;

    if (!pmm_zainicjalizowany) {
        return nullptr;
    }

    uint64_t limit =
        najwyzsza_znaleziona_ramka;

    if (limit > MAX_RAMEK) {
        limit = MAX_RAMEK;
    }

    /*
     * Krytyczny limit obecnej architektury:
     * fizyczne ramki sa nadal czesto dereferencjonowane bez direct-map.
     */
    if (limit > MAX_RAMEK_ALOKOWALNYCH) {
        limit = MAX_RAMEK_ALOKOWALNYCH;
    }

    if (limit == 0) {
        return nullptr;
    }

    if (ostatnia_alokacja >= limit) {
        ostatnia_alokacja = 0;
    }

    /*
     * Przebieg 1: od kursora do konca.
     */
    for (uint64_t ramka = ostatnia_alokacja;
         ramka < limit;
         ++ramka) {

        if (!ramka_dostepna(ramka) ||
            !ramka_wolna(ramka)) {
            continue;
        }

        ustaw_ramke_zajeta(ramka);

        ostatnia_alokacja =
            ramka + 1ULL;

        if (ostatnia_alokacja >= limit) {
            ostatnia_alokacja = 0;
        }

        const uint64_t adres =
            ramka * ROZMIAR_STRONY;

        /*
         * Ramka 0 jest zawsze stale zarezerwowana. Ten test stanowi
         * dodatkowy bezpiecznik, aby nullptr nigdy nie oznaczal sukcesu.
         */
        if (adres == 0) {
            zarezerwuj_ramke_na_stale(
                ramka
            );
            continue;
        }

        return reinterpret_cast<void*>(
            adres
        );
    }

    /*
     * Przebieg 2: zawijamy do poczatku mapy.
     */
    const uint64_t koniec_drugiego_przebiegu =
        ostatnia_alokacja < limit
            ? ostatnia_alokacja
            : limit;

    for (uint64_t ramka = 0;
         ramka < koniec_drugiego_przebiegu;
         ++ramka) {

        if (!ramka_dostepna(ramka) ||
            !ramka_wolna(ramka)) {
            continue;
        }

        ustaw_ramke_zajeta(ramka);

        ostatnia_alokacja =
            ramka + 1ULL;

        if (ostatnia_alokacja >= limit) {
            ostatnia_alokacja = 0;
        }

        const uint64_t adres =
            ramka * ROZMIAR_STRONY;

        if (adres == 0) {
            zarezerwuj_ramke_na_stale(
                ramka
            );
            continue;
        }

        return reinterpret_cast<void*>(
            adres
        );
    }

    return nullptr;
}

/* =========================================================================
 * ZWALNIANIE
 * ========================================================================= */

void ZwolnijRamke(
    void* adres_fizyczny
) {
    if (!adres_fizyczny) {
        return;
    }

    const uint64_t adres =
        reinterpret_cast<uint64_t>(
            adres_fizyczny
        );

    /*
     * ZwolnijRamke wymaga dokladnego adresu poczatku ramki.
     * Nie zaokraglamy uszkodzonego wskaznika, bo moglibysmy zwolnic
     * inna ramke niz ta, ktora wywolujacy rzeczywiscie posiada.
     */
    if ((adres &
         (ROZMIAR_STRONY - 1ULL)) != 0) {
        return;
    }

    BlokadaPMM blokada;

    if (!pmm_zainicjalizowany) {
        return;
    }

    uint64_t numer_ramki = 0;

    if (!adres_na_numer_ramki(
            adres,
            &numer_ramki)) {
        return;
    }

    /*
     * Ramka spoza MEMORY_AVAILABLE albo stale zarezerwowana ma
     * mapa_dostepnosci == 0 i nie moze zostac zwolniona.
     */
    if (!ramka_dostepna(
            numer_ramki)) {
        return;
    }

    /*
     * Double-free: ramka jest juz wolna.
     * Nie zmieniamy stanu drugi raz.
     */
    if (ramka_wolna(
            numer_ramki)) {
        return;
    }

    ustaw_ramke_wolna(
        numer_ramki
    );

    /*
     * Next-fit moze dzieki temu szybko ponownie wykorzystac nizsza
     * zwolniona ramke zamiast czekac na pelne zawiniecie skanowania.
     */
    if (numer_ramki <
        ostatnia_alokacja) {

        ostatnia_alokacja =
            numer_ramki;
    }
}
