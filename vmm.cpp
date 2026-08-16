/*
 * Bursztyn OS - Virtual Memory Manager (VMM) x86_64
 *
 * Aktualny model:
 *  - czteropoziomowe stronicowanie PML4 -> PDP -> PD -> PT,
 *  - bazowa przestrzen jadra posiada supervisor-only identity map 0..4 GiB,
 *  - strona wirtualna 0 jest celowo niezmapowana,
 *  - programy Ring 3 otrzymuja prywatne kopie hierarchii stron,
 *  - flaga USER jest dodawana do przodkow tylko dla konkretnych mapowan
 *    nalezacych do aplikacji,
 *  - istniejace strony 2 MiB sa bezpiecznie rozbijane na strony 4 KiB,
 *  - PMM dostarcza tablice stron ponizej 1 GiB, wiec ich fizyczne adresy
 *    sa obecnie dostepne przez identity map,
 *  - CR0.WP jest wlaczony, aby Ring 0 respektowal strony read-only.
 *
 * WAZNE:
 * API ZmapujStrone() nadal zwraca void dla zgodnosci z obecnym pamiec.h.
 * Wewnatrz VMM wszystkie operacje maja jednak wynik bool i w razie bledu
 * nie publikuja niedokonczonego wpisu liscia.
 */

#include "pamiec.h"

#include <stdint.h>
#include <stddef.h>

/* =========================================================================
 * STALE x86_64
 * ========================================================================= */

namespace {

constexpr uint64_t ROZMIAR_STRONY_4K = 0x1000ULL;
constexpr uint64_t ROZMIAR_STRONY_2M = 0x200000ULL;
constexpr uint64_t ROZMIAR_STRONY_1G = 0x40000000ULL;

constexpr uint64_t MASKA_INDEKSU = 0x1FFULL;

constexpr uint64_t PTE_PRESENT   = 1ULL << 0;
constexpr uint64_t PTE_WRITE     = 1ULL << 1;
constexpr uint64_t PTE_USER      = 1ULL << 2;
constexpr uint64_t PTE_PWT       = 1ULL << 3;
constexpr uint64_t PTE_PCD       = 1ULL << 4;
constexpr uint64_t PTE_ACCESSED  = 1ULL << 5;
constexpr uint64_t PTE_DIRTY     = 1ULL << 6;
constexpr uint64_t PTE_PAT_4K    = 1ULL << 7;
constexpr uint64_t PDE_PS        = 1ULL << 7;
constexpr uint64_t PTE_GLOBAL    = 1ULL << 8;
constexpr uint64_t PDE_PAT_LARGE = 1ULL << 12;
constexpr uint64_t PTE_NX        = 1ULL << 63;

constexpr uint64_t MASKA_ADRESU_4K =
    0x000FFFFFFFFFF000ULL;

constexpr uint64_t MASKA_ADRESU_2M =
    0x000FFFFFFFE00000ULL;

constexpr uint64_t MASKA_ADRESU_1G =
    0x000FFFFFC0000000ULL;

constexpr uint64_t MASKA_FLAG_LISCIA_4K =
    PTE_PRESENT |
    PTE_WRITE |
    PTE_USER |
    PTE_PWT |
    PTE_PCD |
    PTE_ACCESSED |
    PTE_DIRTY |
    PTE_PAT_4K |
    PTE_GLOBAL;

constexpr uint64_t MASKA_FLAG_ZACHOWANYCH_Z_2M =
    PTE_PRESENT |
    PTE_WRITE |
    PTE_USER |
    PTE_PWT |
    PTE_PCD |
    PTE_ACCESSED |
    PTE_DIRTY |
    PTE_GLOBAL |
    PTE_NX;

constexpr uint64_t MASKA_FLAG_ZACHOWANYCH_Z_1G =
    PTE_PRESENT |
    PTE_WRITE |
    PTE_USER |
    PTE_PWT |
    PTE_PCD |
    PTE_ACCESSED |
    PTE_DIRTY |
    PTE_GLOBAL |
    PTE_NX;

/*
 * Page-table frames sa obecnie alokowane przez poprawiony PMM ponizej 1 GiB.
 * Ten limit chroni VMM przed bezposrednim dereferencjonowaniem fizycznej
 * tablicy, ktora nie bylaby dostepna przez identity map.
 */
constexpr uint64_t LIMIT_BEZPOSREDNIEGO_DOSTEPU_FIZ =
    0x40000000ULL;

/*
 * Bazowy identity map zachowujemy do 4 GiB, bo obecne sterowniki AHCI,
 * E1000, LAPIC i IOAPIC nadal korzystaja z fizycznych BAR/MMIO jako
 * adresow wirtualnych.
 */
constexpr uint64_t ROZMIAR_IDENTITY_MAP_JADRA =
    4ULL * 1024ULL * 1024ULL * 1024ULL;

constexpr uint64_t LAPIC_MMIO =
    0xFEE00000ULL;

constexpr uint64_t IOAPIC_MMIO =
    0xFEC00000ULL;

constexpr uint64_t CR0_WP =
    1ULL << 16;

/* =========================================================================
 * STATYCZNE TABLICE STARTOWE JADRA
 * ========================================================================= */

/*
 * Te tablice sa w .bss obrazu jadra, wiec sa dostepne jeszcze pod
 * poczatkowym identity mapping utworzonym przez boot.S.
 */
alignas(4096)
uint64_t st_pml4[512];

alignas(4096)
uint64_t st_pdp[512];

alignas(4096)
uint64_t st_pd[4][512];

/*
 * Pierwsze 2 MiB sa rozbite na 4 KiB, aby strona NULL mogla pozostac
 * niezmapowana.
 */
alignas(4096)
uint64_t st_pt_pierwsze_2m[512];

static_assert(
    sizeof(st_pml4) == ROZMIAR_STRONY_4K,
    "PML4 musi zajmowac jedna strone"
);

static_assert(
    sizeof(st_pdp) == ROZMIAR_STRONY_4K,
    "PDP musi zajmowac jedna strone"
);

static_assert(
    sizeof(st_pd[0]) == ROZMIAR_STRONY_4K,
    "PD musi zajmowac jedna strone"
);

static_assert(
    sizeof(st_pt_pierwsze_2m) == ROZMIAR_STRONY_4K,
    "PT musi zajmowac jedna strone"
);

/* =========================================================================
 * STAN VMM
 * ========================================================================= */

uint64_t* bazowe_pml4_jadra = nullptr;

uint32_t blokada_vmm = 0;

bool vmm_zainicjalizowany = false;

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

static void zablokuj_vmm() {
    while (__atomic_exchange_n(
               &blokada_vmm,
               1U,
               __ATOMIC_ACQUIRE) != 0U) {

        while (__atomic_load_n(
                   &blokada_vmm,
                   __ATOMIC_RELAXED) != 0U) {

            asm volatile("pause");
        }
    }
}

static void odblokuj_vmm() {
    __atomic_store_n(
        &blokada_vmm,
        0U,
        __ATOMIC_RELEASE
    );
}

class BlokadaVMM {
public:
    BlokadaVMM()
        : stan_(wylacz_przerwania()) {
        zablokuj_vmm();
    }

    ~BlokadaVMM() {
        odblokuj_vmm();
        przywroc_przerwania(stan_);
    }

    BlokadaVMM(const BlokadaVMM&) = delete;
    BlokadaVMM& operator=(const BlokadaVMM&) = delete;

private:
    StanPrzerwan stan_;
};

/* =========================================================================
 * CR0 / CR3 / TLB
 * ========================================================================= */

static inline uint64_t odczytaj_cr3() {
    uint64_t cr3 = 0;

    asm volatile(
        "mov %%cr3, %0"
        : "=r"(cr3)
        :
        : "memory"
    );

    return cr3;
}

static inline uint64_t* pml4_z_cr3() {
    const uint64_t fizyczny =
        odczytaj_cr3() &
        MASKA_ADRESU_4K;

    if (fizyczny == 0 ||
        fizyczny >= LIMIT_BEZPOSREDNIEGO_DOSTEPU_FIZ) {
        return nullptr;
    }

    return reinterpret_cast<uint64_t*>(
        fizyczny
    );
}

static inline void zapisz_cr3(
    uint64_t* pml4
) {
    if (!pml4) return;

    const uint64_t adres =
        reinterpret_cast<uint64_t>(
            pml4
        );

    asm volatile(
        "mov %0, %%cr3"
        :
        : "r"(adres)
        : "memory"
    );
}

static inline void uniewaznij_tlb(
    uint64_t adres_wirtualny
) {
    asm volatile(
        "invlpg (%0)"
        :
        : "r"(adres_wirtualny)
        : "memory"
    );
}

static inline void wlacz_cr0_wp() {
    uint64_t cr0 = 0;

    asm volatile(
        "mov %%cr0, %0"
        : "=r"(cr0)
    );

    cr0 |= CR0_WP;

    asm volatile(
        "mov %0, %%cr0"
        :
        : "r"(cr0)
        : "memory"
    );
}

/* =========================================================================
 * WALIDACJA ADRESOW
 * ========================================================================= */

static bool adres_kanoniczny(
    uint64_t adres
) {
    const uint64_t gorne =
        adres >> 48;

    const bool bit47 =
        ((adres >> 47) & 1ULL) != 0;

    if (!bit47) {
        return gorne == 0;
    }

    return gorne == 0xFFFFULL;
}

static bool adres_user_kanoniczny(
    uint64_t adres
) {
    return
        adres_kanoniczny(adres) &&
        ((adres >> 47) & 1ULL) == 0;
}

static bool adres_strony_poprawny(
    uint64_t adres
) {
    return
        adres_kanoniczny(adres) &&
        (adres & (ROZMIAR_STRONY_4K - 1ULL)) == 0;
}

static bool adres_fizyczny_strony_poprawny(
    uint64_t adres
) {
    if ((adres &
         (ROZMIAR_STRONY_4K - 1ULL)) != 0) {
        return false;
    }

    /*
     * Obecny VMM obsluguje fizyczne adresy do 52 bitow.
     */
    return
        (adres & ~MASKA_ADRESU_4K) == 0;
}

static bool tabela_fizyczna_bezposrednio_dostepna(
    uint64_t adres
) {
    return
        adres != 0 &&
        adres < LIMIT_BEZPOSREDNIEGO_DOSTEPU_FIZ &&
        (adres &
         (ROZMIAR_STRONY_4K - 1ULL)) == 0;
}

/* =========================================================================
 * INDEKSY TABLIC
 * ========================================================================= */

static inline uint16_t indeks_pml4(
    uint64_t adres
) {
    return static_cast<uint16_t>(
        (adres >> 39) &
        MASKA_INDEKSU
    );
}

static inline uint16_t indeks_pdp(
    uint64_t adres
) {
    return static_cast<uint16_t>(
        (adres >> 30) &
        MASKA_INDEKSU
    );
}

static inline uint16_t indeks_pd(
    uint64_t adres
) {
    return static_cast<uint16_t>(
        (adres >> 21) &
        MASKA_INDEKSU
    );
}

static inline uint16_t indeks_pt(
    uint64_t adres
) {
    return static_cast<uint16_t>(
        (adres >> 12) &
        MASKA_INDEKSU
    );
}

/* =========================================================================
 * OPERACJE NA STRONACH TABLIC
 * ========================================================================= */

void WyzerujStrone(
    void* wskaznik
) {
    if (!wskaznik) return;

    uint64_t* tabela =
        static_cast<uint64_t*>(
            wskaznik
        );

    for (size_t i = 0;
         i < 512;
         ++i) {

        tabela[i] = 0;
    }
}

static uint64_t* alokuj_tabele_stron() {
    void* ramka =
        ZaalokujRamke();

    if (!ramka) {
        return nullptr;
    }

    const uint64_t fiz =
        reinterpret_cast<uint64_t>(
            ramka
        );

    if (!tabela_fizyczna_bezposrednio_dostepna(
            fiz)) {

        /*
         * PMM nie powinien obecnie wydac takiej ramki.
         */
        ZwolnijRamke(ramka);
        return nullptr;
    }

    WyzerujStrone(
        ramka
    );

    return static_cast<uint64_t*>(
        ramka
    );
}

static uint64_t* tabela_z_wpisu(
    uint64_t wpis
) {
    if ((wpis & PTE_PRESENT) == 0) {
        return nullptr;
    }

    const uint64_t fiz =
        wpis &
        MASKA_ADRESU_4K;

    if (!tabela_fizyczna_bezposrednio_dostepna(
            fiz)) {
        return nullptr;
    }

    return reinterpret_cast<uint64_t*>(
        fiz
    );
}

static uint64_t flagi_tabeli_posredniej(
    bool user
) {
    uint64_t flagi =
        PTE_PRESENT |
        PTE_WRITE;

    if (user) {
        flagi |= PTE_USER;
    }

    return flagi;
}

/* =========================================================================
 * ROZBIJANIE DUZYCH STRON
 * ========================================================================= */

static bool rozbij_strone_1g(
    uint64_t* pdp,
    uint16_t indeks,
    bool wymagaj_user
) {
    if (!pdp) return false;

    const uint64_t stary =
        pdp[indeks];

    if ((stary & PTE_PRESENT) == 0 ||
        (stary & PDE_PS) == 0) {
        return false;
    }

    uint64_t* nowy_pd =
        alokuj_tabele_stron();

    if (!nowy_pd) {
        return false;
    }

    const uint64_t baza =
        stary &
        MASKA_ADRESU_1G;

    uint64_t flagi_liscia =
        stary &
        MASKA_FLAG_ZACHOWANYCH_Z_1G;

    if ((stary & PDE_PAT_LARGE) != 0) {
        flagi_liscia |=
            PDE_PAT_LARGE;
    }

    /*
     * Po rozbiciu 1 GiB otrzymujemy 512 stron po 2 MiB.
     */
    for (uint64_t i = 0;
         i < 512;
         ++i) {

        nowy_pd[i] =
            (baza +
             i * ROZMIAR_STRONY_2M) |
            flagi_liscia |
            PDE_PS;
    }

    const bool user =
        wymagaj_user ||
        ((stary & PTE_USER) != 0);

    pdp[indeks] =
        (reinterpret_cast<uint64_t>(
            nowy_pd) &
         MASKA_ADRESU_4K) |
        flagi_tabeli_posredniej(user);

    return true;
}

static bool rozbij_strone_2m(
    uint64_t* pd,
    uint16_t indeks,
    bool wymagaj_user
) {
    if (!pd) return false;

    const uint64_t stary =
        pd[indeks];

    if ((stary & PTE_PRESENT) == 0 ||
        (stary & PDE_PS) == 0) {
        return false;
    }

    uint64_t* nowy_pt =
        alokuj_tabele_stron();

    if (!nowy_pt) {
        return false;
    }

    const uint64_t baza =
        stary &
        MASKA_ADRESU_2M;

    uint64_t flagi_liscia =
        stary &
        MASKA_FLAG_ZACHOWANYCH_Z_2M;

    /*
     * PAT dla duzej strony znajduje sie w bicie 12.
     * W PTE 4 KiB odpowiada mu bit 7.
     */
    if ((stary & PDE_PAT_LARGE) != 0) {
        flagi_liscia |=
            PTE_PAT_4K;
    }

    for (uint64_t i = 0;
         i < 512;
         ++i) {

        nowy_pt[i] =
            (baza +
             i * ROZMIAR_STRONY_4K) |
            flagi_liscia;
    }

    const bool user =
        wymagaj_user ||
        ((stary & PTE_USER) != 0);

    pd[indeks] =
        (reinterpret_cast<uint64_t>(
            nowy_pt) &
         MASKA_ADRESU_4K) |
        flagi_tabeli_posredniej(user);

    return true;
}

/* =========================================================================
 * TWORZENIE POSREDNICH TABLIC
 * ========================================================================= */

static bool pobierz_lub_utworz_pdp(
    uint64_t* pml4,
    uint16_t indeks,
    bool user,
    uint64_t** wynik
) {
    if (!pml4 || !wynik) return false;

    uint64_t& wpis =
        pml4[indeks];

    if ((wpis & PTE_PRESENT) == 0) {
        uint64_t* nowa =
            alokuj_tabele_stron();

        if (!nowa) return false;

        wpis =
            (reinterpret_cast<uint64_t>(
                nowa) &
             MASKA_ADRESU_4K) |
            flagi_tabeli_posredniej(user);
    } else {
        /*
         * Bit PS w PML4 jest zarezerwowany.
         */
        if ((wpis & PDE_PS) != 0) {
            return false;
        }

        if (user) {
            wpis |= PTE_USER;
        }

        wpis |=
            PTE_PRESENT |
            PTE_WRITE;
    }

    *wynik =
        tabela_z_wpisu(wpis);

    return *wynik != nullptr;
}

static bool pobierz_lub_utworz_pd(
    uint64_t* pdp,
    uint16_t indeks,
    bool user,
    uint64_t** wynik
) {
    if (!pdp || !wynik) return false;

    uint64_t& wpis =
        pdp[indeks];

    if ((wpis & PTE_PRESENT) != 0 &&
        (wpis & PDE_PS) != 0) {

        if (!rozbij_strone_1g(
                pdp,
                indeks,
                user)) {
            return false;
        }
    }

    if ((wpis & PTE_PRESENT) == 0) {
        uint64_t* nowa =
            alokuj_tabele_stron();

        if (!nowa) return false;

        wpis =
            (reinterpret_cast<uint64_t>(
                nowa) &
             MASKA_ADRESU_4K) |
            flagi_tabeli_posredniej(user);
    } else {
        if (user) {
            wpis |= PTE_USER;
        }

        wpis |=
            PTE_PRESENT |
            PTE_WRITE;
    }

    *wynik =
        tabela_z_wpisu(wpis);

    return *wynik != nullptr;
}

static bool pobierz_lub_utworz_pt(
    uint64_t* pd,
    uint16_t indeks,
    bool user,
    uint64_t** wynik
) {
    if (!pd || !wynik) return false;

    uint64_t& wpis =
        pd[indeks];

    if ((wpis & PTE_PRESENT) != 0 &&
        (wpis & PDE_PS) != 0) {

        if (!rozbij_strone_2m(
                pd,
                indeks,
                user)) {
            return false;
        }
    }

    if ((wpis & PTE_PRESENT) == 0) {
        uint64_t* nowa =
            alokuj_tabele_stron();

        if (!nowa) return false;

        wpis =
            (reinterpret_cast<uint64_t>(
                nowa) &
             MASKA_ADRESU_4K) |
            flagi_tabeli_posredniej(user);
    } else {
        if (user) {
            wpis |= PTE_USER;
        }

        wpis |=
            PTE_PRESENT |
            PTE_WRITE;
    }

    *wynik =
        tabela_z_wpisu(wpis);

    return *wynik != nullptr;
}

/* =========================================================================
 * MAPOWANIE STRONY 4 KiB
 * ========================================================================= */

static bool mapuj_strone_wewnetrznie(
    uint64_t* pml4,
    uint64_t adres_wirtualny,
    uint64_t adres_fizyczny,
    uint32_t flagi
) {
    if (!pml4) return false;

    if (!adres_strony_poprawny(
            adres_wirtualny)) {
        return false;
    }

    if (!adres_fizyczny_strony_poprawny(
            adres_fizyczny)) {
        return false;
    }

    const bool user =
        (static_cast<uint64_t>(flagi) &
         PTE_USER) != 0;

    if (user) {
        if (!adres_user_kanoniczny(
                adres_wirtualny)) {
            return false;
        }

        /*
         * Nie pozwalamy Ring 3 mapowac strony NULL.
         */
        if (adres_wirtualny <
            ROZMIAR_STRONY_4K) {
            return false;
        }
    }

    uint64_t* pdp = nullptr;
    uint64_t* pd = nullptr;
    uint64_t* pt = nullptr;

    if (!pobierz_lub_utworz_pdp(
            pml4,
            indeks_pml4(adres_wirtualny),
            user,
            &pdp)) {
        return false;
    }

    if (!pobierz_lub_utworz_pd(
            pdp,
            indeks_pdp(adres_wirtualny),
            user,
            &pd)) {
        return false;
    }

    if (!pobierz_lub_utworz_pt(
            pd,
            indeks_pd(adres_wirtualny),
            user,
            &pt)) {
        return false;
    }

    uint64_t flagi_liscia =
        static_cast<uint64_t>(flagi) &
        MASKA_FLAG_LISCIA_4K;

    /*
     * ZmapujStrone oznacza utworzenie aktywnego mapowania.
     */
    flagi_liscia |=
        PTE_PRESENT;

    pt[indeks_pt(adres_wirtualny)] =
        (adres_fizyczny &
         MASKA_ADRESU_4K) |
        flagi_liscia;

    uniewaznij_tlb(
        adres_wirtualny
    );

    return true;
}

/* =========================================================================
 * WALKER TABLIC STRON
 * ========================================================================= */

struct WynikWalkera {
    bool obecna;
    bool user;
    bool zapisywalna;
    bool duza;
    uint64_t wpis;
    uint64_t rozmiar_strony;
};

static WynikWalkera sprawdz_mapowanie(
    uint64_t* pml4,
    uint64_t adres
) {
    WynikWalkera wynik{};

    if (!pml4 ||
        !adres_kanoniczny(adres)) {
        return wynik;
    }

    const uint64_t e4 =
        pml4[indeks_pml4(adres)];

    if ((e4 & PTE_PRESENT) == 0) {
        return wynik;
    }

    bool user =
        (e4 & PTE_USER) != 0;

    bool write =
        (e4 & PTE_WRITE) != 0;

    uint64_t* pdp =
        tabela_z_wpisu(e4);

    if (!pdp) {
        return wynik;
    }

    const uint64_t e3 =
        pdp[indeks_pdp(adres)];

    if ((e3 & PTE_PRESENT) == 0) {
        return wynik;
    }

    user =
        user &&
        ((e3 & PTE_USER) != 0);

    write =
        write &&
        ((e3 & PTE_WRITE) != 0);

    if ((e3 & PDE_PS) != 0) {
        wynik.obecna = true;
        wynik.user = user;
        wynik.zapisywalna = write;
        wynik.duza = true;
        wynik.wpis = e3;
        wynik.rozmiar_strony =
            ROZMIAR_STRONY_1G;

        return wynik;
    }

    uint64_t* pd =
        tabela_z_wpisu(e3);

    if (!pd) {
        return wynik;
    }

    const uint64_t e2 =
        pd[indeks_pd(adres)];

    if ((e2 & PTE_PRESENT) == 0) {
        return wynik;
    }

    user =
        user &&
        ((e2 & PTE_USER) != 0);

    write =
        write &&
        ((e2 & PTE_WRITE) != 0);

    if ((e2 & PDE_PS) != 0) {
        wynik.obecna = true;
        wynik.user = user;
        wynik.zapisywalna = write;
        wynik.duza = true;
        wynik.wpis = e2;
        wynik.rozmiar_strony =
            ROZMIAR_STRONY_2M;

        return wynik;
    }

    uint64_t* pt =
        tabela_z_wpisu(e2);

    if (!pt) {
        return wynik;
    }

    const uint64_t e1 =
        pt[indeks_pt(adres)];

    if ((e1 & PTE_PRESENT) == 0) {
        return wynik;
    }

    user =
        user &&
        ((e1 & PTE_USER) != 0);

    write =
        write &&
        ((e1 & PTE_WRITE) != 0);

    wynik.obecna = true;
    wynik.user = user;
    wynik.zapisywalna = write;
    wynik.duza = false;
    wynik.wpis = e1;
    wynik.rozmiar_strony =
        ROZMIAR_STRONY_4K;

    return wynik;
}

/* =========================================================================
 * KLONOWANIE HIERARCHII
 * ========================================================================= */

/*
 * poziom:
 *   4 = PML4
 *   3 = PDP
 *   2 = PD
 *   1 = PT
 */
static void zwolnij_sklonowane_tabele_bez_lisci(
    uint64_t* tabela,
    int poziom
) {
    if (!tabela ||
        poziom < 1 ||
        poziom > 4) {
        return;
    }

    if (poziom > 1) {
        for (size_t i = 0;
             i < 512;
             ++i) {

            const uint64_t wpis =
                tabela[i];

            if ((wpis & PTE_PRESENT) == 0) {
                continue;
            }

            if ((poziom == 3 ||
                 poziom == 2) &&
                (wpis & PDE_PS) != 0) {
                continue;
            }

            uint64_t* dziecko =
                tabela_z_wpisu(wpis);

            if (!dziecko) {
                continue;
            }

            zwolnij_sklonowane_tabele_bez_lisci(
                dziecko,
                poziom - 1
            );
        }
    }

    ZwolnijRamke(
        tabela
    );
}

static uint64_t* sklonuj_hierarchie(
    const uint64_t* zrodlo,
    int poziom
) {
    if (!zrodlo ||
        poziom < 1 ||
        poziom > 4) {
        return nullptr;
    }

    uint64_t* kopia =
        alokuj_tabele_stron();

    if (!kopia) {
        return nullptr;
    }

    for (size_t i = 0;
         i < 512;
         ++i) {

        const uint64_t wpis =
            zrodlo[i];

        if ((wpis & PTE_PRESENT) == 0) {
            /*
             * Kopiujemy rowniez nie-present software bits tylko wtedy,
             * gdy w przyszlosci VMM zacznie ich uzywac. Obecnie zero jest
             * najbezpieczniejsze.
             */
            kopia[i] = 0;
            continue;
        }

        /*
         * PT zawiera juz liscie 4 KiB.
         */
        if (poziom == 1) {
            kopia[i] = wpis;
            continue;
        }

        /*
         * 1 GiB na poziomie PDP albo 2 MiB na poziomie PD sa liscmi
         * i pozostaja wspoldzielonym mapowaniem fizycznym.
         */
        if ((poziom == 3 ||
             poziom == 2) &&
            (wpis & PDE_PS) != 0) {

            kopia[i] = wpis;
            continue;
        }

        const uint64_t fiz_dziecka =
            wpis &
            MASKA_ADRESU_4K;

        if (!tabela_fizyczna_bezposrednio_dostepna(
                fiz_dziecka)) {

            zwolnij_sklonowane_tabele_bez_lisci(
                kopia,
                poziom
            );

            return nullptr;
        }

        const uint64_t* stare_dziecko =
            reinterpret_cast<const uint64_t*>(
                fiz_dziecka
            );

        uint64_t* nowe_dziecko =
            sklonuj_hierarchie(
                stare_dziecko,
                poziom - 1
            );

        if (!nowe_dziecko) {
            zwolnij_sklonowane_tabele_bez_lisci(
                kopia,
                poziom
            );

            return nullptr;
        }

        kopia[i] =
            (reinterpret_cast<uint64_t>(
                nowe_dziecko) &
             MASKA_ADRESU_4K) |
            (wpis & ~MASKA_ADRESU_4K);
    }

    return kopia;
}

/* =========================================================================
 * NISZCZENIE PRZESTRZENI PROCESU
 * ========================================================================= */

static void zniszcz_hierarchie_procesu(
    uint64_t* tabela,
    int poziom
) {
    if (!tabela ||
        poziom < 1 ||
        poziom > 4) {
        return;
    }

    if (poziom == 1) {
        /*
         * Na poziomie PT zwalniamy tylko fizyczne ramki lisci oznaczonych
         * USER. Supervisor-only liscie sa wspoldzielonymi mapowaniami jadra.
         */
        for (size_t i = 0;
             i < 512;
             ++i) {

            const uint64_t wpis =
                tabela[i];

            if ((wpis & PTE_PRESENT) == 0 ||
                (wpis & PTE_USER) == 0) {
                continue;
            }

            const uint64_t fiz =
                wpis &
                MASKA_ADRESU_4K;

            if (fiz != 0) {
                ZwolnijRamke(
                    reinterpret_cast<void*>(
                        fiz
                    )
                );
            }

            tabela[i] = 0;
        }

        ZwolnijRamke(tabela);
        return;
    }

    for (size_t i = 0;
         i < 512;
         ++i) {

        const uint64_t wpis =
            tabela[i];

        if ((wpis & PTE_PRESENT) == 0) {
            continue;
        }

        /*
         * Loader Bursztyna nie tworzy user huge-pages.
         * Nie zwalniamy fizycznej pamieci takiego liscia automatycznie,
         * bo moglby byc mapowaniem wspoldzielonym/MMIO.
         */
        if ((poziom == 3 ||
             poziom == 2) &&
            (wpis & PDE_PS) != 0) {
            continue;
        }

        uint64_t* dziecko =
            tabela_z_wpisu(wpis);

        if (!dziecko) {
            continue;
        }

        zniszcz_hierarchie_procesu(
            dziecko,
            poziom - 1
        );
    }

    ZwolnijRamke(tabela);
}

/* =========================================================================
 * BUDOWA BAZOWEJ PRZESTRZENI JADRA
 * ========================================================================= */

static void przygotuj_identity_map_jadra() {
    WyzerujStrone(st_pml4);
    WyzerujStrone(st_pdp);

    for (size_t i = 0;
         i < 4;
         ++i) {
        WyzerujStrone(st_pd[i]);
    }

    WyzerujStrone(
        st_pt_pierwsze_2m
    );

    /*
     * Brak PTE_USER jest tutaj celowy.
     * Ring 3 nie moze otrzymac dostepu do calej fizycznej pamieci tylko
     * dlatego, ze aplikacja uzywa adresow w tej samej czesci PML4.
     */
    st_pml4[0] =
        (reinterpret_cast<uint64_t>(
            st_pdp) &
         MASKA_ADRESU_4K) |
        PTE_PRESENT |
        PTE_WRITE;

    for (uint64_t gigabajt = 0;
         gigabajt < 4;
         ++gigabajt) {

        st_pdp[gigabajt] =
            (reinterpret_cast<uint64_t>(
                st_pd[gigabajt]) &
             MASKA_ADRESU_4K) |
            PTE_PRESENT |
            PTE_WRITE;
    }

    /*
     * Pierwsze 2 MiB jako 4 KiB.
     * PTE[0] = 0 -> NULL dereference wywola #PF.
     */
    st_pd[0][0] =
        (reinterpret_cast<uint64_t>(
            st_pt_pierwsze_2m) &
         MASKA_ADRESU_4K) |
        PTE_PRESENT |
        PTE_WRITE;

    st_pt_pierwsze_2m[0] = 0;

    for (uint64_t i = 1;
         i < 512;
         ++i) {

        st_pt_pierwsze_2m[i] =
            i * ROZMIAR_STRONY_4K |
            PTE_PRESENT |
            PTE_WRITE;
    }

    /*
     * Reszta 0..4 GiB jako supervisor-only strony 2 MiB.
     */
    for (uint64_t gigabajt = 0;
         gigabajt < 4;
         ++gigabajt) {

        for (uint64_t i = 0;
             i < 512;
             ++i) {

            if (gigabajt == 0 &&
                i == 0) {
                continue;
            }

            const uint64_t fiz =
                gigabajt *
                    ROZMIAR_STRONY_1G +
                i *
                    ROZMIAR_STRONY_2M;

            uint64_t flagi =
                PTE_PRESENT |
                PTE_WRITE |
                PDE_PS;

            /*
             * MMIO APIC nie powinno korzystac ze zwyklego cache WB.
             * PWT+PCD zachowuje kompatybilnosc z obecnym apic.cpp.
             */
            if (fiz == LAPIC_MMIO ||
                fiz == IOAPIC_MMIO) {

                flagi |=
                    PTE_PWT |
                    PTE_PCD;
            }

            st_pd[gigabajt][i] =
                (fiz &
                 MASKA_ADRESU_2M) |
                flagi;
        }
    }
}

} // namespace

/* =========================================================================
 * PUBLICZNY WSKAZNIK DLA ZGODNOSCI ZE STARYM KODEM
 * ========================================================================= */

/*
 * Nie nalezy traktowac tej zmiennej jako jedynego zrodla prawdy.
 * Scheduler moze przelaczyc CR3 w asemblerze. Publiczne funkcje VMM
 * odczytuja aktualny CR3 bezposrednio z procesora.
 */
uint64_t* globalne_pml4 = nullptr;

/* =========================================================================
 * MAPOWANIE
 * ========================================================================= */

void ZmapujStrone(
    void* adres_wirtualny,
    void* adres_fizyczny,
    uint32_t flagi
) {
    BlokadaVMM blokada;

    if (!vmm_zainicjalizowany) {
        return;
    }

    uint64_t* aktualne =
        pml4_z_cr3();

    if (!aktualne) {
        return;
    }

    globalne_pml4 =
        aktualne;

    (void)mapuj_strone_wewnetrznie(
        aktualne,
        reinterpret_cast<uint64_t>(
            adres_wirtualny),
        reinterpret_cast<uint64_t>(
            adres_fizyczny),
        flagi
    );
}

/* =========================================================================
 * INICJALIZACJA
 * ========================================================================= */

void InicjalizujVMM() {
    BlokadaVMM blokada;

    /*
     * Ponowna inicjalizacja VMM podczas pracy procesow bylaby destrukcyjna.
     */
    if (vmm_zainicjalizowany) {
        return;
    }

    przygotuj_identity_map_jadra();

    bazowe_pml4_jadra =
        st_pml4;

    globalne_pml4 =
        st_pml4;

    /*
     * Wszystkie tablice statyczne znajduja sie w obrazie jadra, ktory
     * boot.S mapuje przed wejsciem tutaj.
     */
    zapisz_cr3(
        st_pml4
    );

    /*
     * Bez CR0.WP zapis z Ring 0 omija read-only PTE. Loader mapuje kod
     * aplikacji bez PTE_WRITE, wiec WP jest potrzebny, aby ten zakaz byl
     * rzeczywiscie respektowany rowniez przez jadro.
     */
    wlacz_cr0_wp();

    vmm_zainicjalizowany = true;
}

/* =========================================================================
 * AKTUALNE PML4
 * ========================================================================= */

extern "C" void* PobierzAktualnePML4() {
    uint64_t* aktualne =
        pml4_z_cr3();

    if (!aktualne) {
        return nullptr;
    }

    globalne_pml4 =
        aktualne;

    return aktualne;
}

/* =========================================================================
 * NOWA PRZESTRZEN PROCESU
 * ========================================================================= */

void* UtworzPrzestrzenAdresowaProcesu() {
    BlokadaVMM blokada;

    if (!vmm_zainicjalizowany ||
        !bazowe_pml4_jadra) {
        return nullptr;
    }

    /*
     * Zawsze klonujemy bazowa przestrzen JADRA, a nie aktualne CR3.
     * Gdy proces tworzy kolejny proces przez syscall, jego prywatne mapowania
     * Ring 3 nie moga przeciec do nowego programu.
     */
    uint64_t* nowe_pml4 =
        sklonuj_hierarchie(
            bazowe_pml4_jadra,
            4
        );

    return nowe_pml4;
}

/* =========================================================================
 * ZMIANA PRZESTRZENI ADRESOWEJ
 * ========================================================================= */

void UstawPrzestrzenAdresowa(
    void* pml4
) {
    if (!pml4) {
        return;
    }

    const uint64_t adres =
        reinterpret_cast<uint64_t>(
            pml4
        );

    if (!tabela_fizyczna_bezposrednio_dostepna(
            adres)) {
        return;
    }

    BlokadaVMM blokada;

    if (!vmm_zainicjalizowany) {
        return;
    }

    globalne_pml4 =
        static_cast<uint64_t*>(
            pml4
        );

    zapisz_cr3(
        globalne_pml4
    );
}

/* =========================================================================
 * NISZCZENIE PRYWATNEJ PRZESTRZENI PROCESU
 * ========================================================================= */

extern "C" void ZniszczPrzestrzenAdresowaProcesu(
    void* pml4
) {
    if (!pml4) {
        return;
    }

    uint64_t* do_zniszczenia =
        static_cast<uint64_t*>(
            pml4
        );

    BlokadaVMM blokada;

    if (!vmm_zainicjalizowany ||
        !bazowe_pml4_jadra) {
        return;
    }

    if (do_zniszczenia ==
        bazowe_pml4_jadra) {
        return;
    }

    /*
     * Nigdy nie zwalniamy tablic, z ktorych CPU aktualnie korzysta.
     */
    if (do_zniszczenia ==
        pml4_z_cr3()) {
        return;
    }

    const uint64_t fiz =
        reinterpret_cast<uint64_t>(
            do_zniszczenia
        );

    if (!tabela_fizyczna_bezposrednio_dostepna(
            fiz)) {
        return;
    }

    zniszcz_hierarchie_procesu(
        do_zniszczenia,
        4
    );
}

/* =========================================================================
 * WALIDACJA ZAKRESU RING 3 DLA copy_from_user / copy_to_user
 * ========================================================================= */

extern "C" bool bws_vmm_sprawdz_zakres_uzytkownika(
    const void* adres,
    size_t rozmiar,
    bool zapis
) {
    /*
     * Kopiowanie zera bajtow nie dereferencjonuje wskaznika.
     */
    if (rozmiar == 0) {
        return true;
    }

    if (!adres) {
        return false;
    }

    const uint64_t poczatek =
        reinterpret_cast<uint64_t>(
            adres
        );

    if (!adres_user_kanoniczny(
            poczatek) ||
        poczatek <
            ROZMIAR_STRONY_4K) {
        return false;
    }

    const uint64_t minus_jeden =
        static_cast<uint64_t>(
            rozmiar - 1U
        );

    if (poczatek >
        UINT64_MAX - minus_jeden) {
        return false;
    }

    const uint64_t koniec =
        poczatek +
        minus_jeden;

    if (!adres_user_kanoniczny(
            koniec)) {
        return false;
    }

    BlokadaVMM blokada;

    if (!vmm_zainicjalizowany) {
        return false;
    }

    uint64_t* aktualne =
        pml4_z_cr3();

    if (!aktualne) {
        return false;
    }

    /*
     * Sprawdzamy kazda dotknieta strone. Dla huge-page walker zwraca
     * uprawnienia calego liscia, ale krok 4 KiB upraszcza kod i eliminuje
     * ryzyko bledu na granicy mapowan.
     */
    uint64_t strona =
        poczatek &
        ~(ROZMIAR_STRONY_4K - 1ULL);

    const uint64_t ostatnia_strona =
        koniec &
        ~(ROZMIAR_STRONY_4K - 1ULL);

    while (true) {
        const WynikWalkera wynik =
            sprawdz_mapowanie(
                aktualne,
                strona
            );

        if (!wynik.obecna ||
            !wynik.user) {
            return false;
        }

        if (zapis &&
            !wynik.zapisywalna) {
            return false;
        }

        if (strona ==
            ostatnia_strona) {
            break;
        }

        if (strona >
            UINT64_MAX -
            ROZMIAR_STRONY_4K) {
            return false;
        }

        strona +=
            ROZMIAR_STRONY_4K;
    }

    return true;
}
