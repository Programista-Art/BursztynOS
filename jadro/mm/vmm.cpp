/*
 * Bursztyn OS - Virtual Memory Manager (VMM) x86_64
 *
 * Aktualny model:
 *  - czteropoziomowe stronicowanie PML4 -> PDP -> PD -> PT,
 *  - bazowa przestrzen jadra posiada supervisor-only identity map 0..4 GiB,
 *  - HHDM mapuje wylacznie zwykly RAM wskazany przez Multiboot2,
 *  - strona wirtualna 0 jest celowo niezmapowana,
 *  - programy Ring 3 otrzymuja prywatne kopie hierarchii stron,
 *  - flaga USER jest dodawana do przodkow tylko dla konkretnych mapowan
 *    nalezacych do aplikacji,
 *  - istniejace strony 2 MiB sa bezpiecznie rozbijane na strony 4 KiB,
 *  - dynamiczne tablice stron sa dereferencjonowane wylacznie przez HHDM,
 *  - CR0.WP jest wlaczony, aby Ring 0 respektowal strony read-only.
 *
 * WAZNE:
 * API ZmapujStrone() nadal zwraca void dla zgodnosci z obecnym pamiec.h.
 * Wewnatrz VMM wszystkie operacje maja jednak wynik bool i w razie bledu
 * nie publikuja niedokonczonego wpisu liscia.
 */

#include "vmm_zakres.h"
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

/*
 * x86_64 dzieli kanoniczna przestrzen adresowa na dwie polowy PML4.
 * Poddrzewa 0..255 naleza do przestrzeni procesu, a 256..511 sa
 * wspoldzielonymi mapowaniami jadra. Ta sama granica obowiazuje przy
 * klonowaniu, zwijaniu nieudanego klonu i niszczeniu procesu.
 */
constexpr size_t PIERWSZY_WSPOLDZIELONY_PML4_JADRA = 256;

static bool pml4_wpis_jadra(size_t indeks) {
    return indeks >= PIERWSZY_WSPOLDZIELONY_PML4_JADRA;
}

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
constexpr uint64_t PTE_SHARED    = 1ULL << 9;
constexpr uint64_t PDE_PAT_LARGE = 1ULL << 12;
constexpr uint64_t PTE_NX        = 1ULL << 63;

constexpr uint64_t MASKA_ADRESU_4K =
    0x000FFFFFFFFFF000ULL;

constexpr uint64_t MASKA_ADRESU_2M =
    0x000FFFFFFFE00000ULL;

constexpr uint64_t MASKA_ADRESU_1G =
    0x000FFFFFC0000000ULL;

/*
 * Maska flag liscia 4 KiB MUSI zawierac PTE_NX.
 *
 * Bez bitu 63 kazda strona Ring 3 pozostawala wykonywalna, nawet gdy
 * EFER.NXE bylo wlaczone - PTE_NX byl po prostu obcinany przez maske.
 * To lamalo deklarowane W^X dla .data, sterty i stosu aplikacji.
 */
constexpr uint64_t MASKA_FLAG_LISCIA_4K =
    PTE_PRESENT |
    PTE_WRITE |
    PTE_USER |
    PTE_PWT |
    PTE_PCD |
    PTE_ACCESSED |
    PTE_DIRTY |
    PTE_PAT_4K |
    PTE_GLOBAL |
    PTE_SHARED |
    PTE_NX;

static_assert(
    (MASKA_FLAG_LISCIA_4K & PTE_NX) != 0,
    "Maska flag liscia 4 KiB musi zachowywac bit NX (bit 63)"
);

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

constexpr uint32_t CPUID_EXTENDED_FEATURES =
    0x80000001U;

constexpr uint32_t CPUID_NX_BIT =
    1U << 20;

/*
 * CPUID.(EAX=7,ECX=0):EBX
 *   bit 7  = SMEP (Supervisor Mode Execution Prevention)
 *   bit 20 = SMAP (Supervisor Mode Access Prevention)
 */
constexpr uint32_t CPUID_STRUCTURED_FEATURES =
    7U;

constexpr uint32_t CPUID_SMEP_BIT =
    1U << 7;

constexpr uint32_t CPUID_SMAP_BIT =
    1U << 20;

/*
 * CR4:
 *   bit 20 = SMEP
 *   bit 21 = SMAP
 */
constexpr uint64_t CR4_SMEP =
    1ULL << 20;

constexpr uint64_t CR4_SMAP =
    1ULL << 21;

constexpr uint32_t IA32_EFER_MSR =
    0xC0000080U;

constexpr uint64_t IA32_EFER_NXE =
    1ULL << 11;

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
 * HHDM ma osobna hierarchie, aby nigdy nie dziedziczyl identity-map MMIO.
 * Pula PT obsluguje fragmenty 2 MiB na brzegach regionow bez rozszerzania
 * zakresow do MMIO. Przepelnienie konczy inicjalizacje fail-closed.
 */
alignas(4096)
uint64_t st_hhdm_pdp[512];

alignas(4096)
uint64_t st_hhdm_pd[4][512];

constexpr size_t MAKS_CZESCIOWYCH_PT_HHDM = 12;

alignas(4096)
uint64_t st_hhdm_pt[MAKS_CZESCIOWYCH_PT_HHDM][512];

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

bool nx_aktywny = false;

bool smep_aktywny = false;

bool smap_aktywny = false;

static bool tabela_fizyczna_bezposrednio_dostepna(
    uint64_t adres
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

    if (!tabela_fizyczna_bezposrednio_dostepna(
            fizyczny)) {
        return nullptr;
    }

    return static_cast<uint64_t*>(
        FizycznyZakresDoWirtualnego(
            fizyczny,
            ROZMIAR_STRONY_4K
        )
    );
}

static inline void zapisz_cr3(
    uint64_t* pml4
) {
    if (!pml4) return;

    uint64_t adres = 0;
    if (!WirtualnyDoFizycznego(
            pml4,
            &adres) ||
        !tabela_fizyczna_bezposrednio_dostepna(
            adres)) {
        return;
    }

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

static void wlacz_nx_jezeli_obslugiwane() {
    uint32_t maksymalny_lisc = 0;

    asm volatile(
        "cpuid"
        : "=a"(maksymalny_lisc)
        : "a"(0x80000000U)
        : "rbx", "rcx", "rdx", "memory"
    );

    if (maksymalny_lisc <
        CPUID_EXTENDED_FEATURES) {
        nx_aktywny = false;
        return;
    }

    uint32_t edx = 0;
    uint32_t eax = 0;
    uint32_t ebx = 0;
    uint32_t ecx = 0;

    asm volatile(
        "cpuid"
        : "=a"(eax),
          "=b"(ebx),
          "=c"(ecx),
          "=d"(edx)
        : "a"(CPUID_EXTENDED_FEATURES)
        : "memory"
    );

    if ((edx & CPUID_NX_BIT) == 0) {
        nx_aktywny = false;
        return;
    }

    uint32_t efer_lo = 0;
    uint32_t efer_hi = 0;

    asm volatile(
        "rdmsr"
        : "=a"(efer_lo),
          "=d"(efer_hi)
        : "c"(IA32_EFER_MSR)
        : "memory"
    );

    uint64_t efer =
        static_cast<uint64_t>(efer_lo) |
        (static_cast<uint64_t>(efer_hi) << 32);

    efer |= IA32_EFER_NXE;
    efer_lo = static_cast<uint32_t>(efer);
    efer_hi = static_cast<uint32_t>(efer >> 32);

    asm volatile(
        "wrmsr"
        :
        : "c"(IA32_EFER_MSR),
          "a"(efer_lo),
          "d"(efer_hi)
        : "memory"
    );

    nx_aktywny = true;
}

/*
 * Wlacza SMEP i SMAP, jezeli procesor je obsluguje.
 *
 * SMEP: Ring 0 nie moze WYKONYWAC kodu z pamieci Ring 3. Zamyka cala klase
 * atakow "ret2usr", w ktorej jadro zostaje zmuszone do skoku pod adres
 * kontrolowany przez aplikacje.
 *
 * SMAP: Ring 0 nie moze CZYTAC ani PISAC pamieci Ring 3 bez flagi AC.
 * Wymaga to istnienia straznikow stac()/clt() wokol kazdego celowego
 * dostepu - patrz OknoUzytkownika w pamiec.h.
 *
 * Brak wsparcia sprzetowego nie jest bledem: system dziala dalej, tylko
 * z slabsza izolacja. Flaga jest raportowana w diagnostyce.
 */
static void wlacz_smep_smap_jezeli_obslugiwane() {
    uint32_t maksymalny_lisc = 0;

    asm volatile(
        "cpuid"
        : "=a"(maksymalny_lisc)
        : "a"(0U)
        : "rbx", "rcx", "rdx", "memory"
    );

    if (maksymalny_lisc < CPUID_STRUCTURED_FEATURES) {
        return;
    }

    uint32_t eax = CPUID_STRUCTURED_FEATURES;
    uint32_t ebx = 0;
    uint32_t ecx = 0;
    uint32_t edx = 0;

    asm volatile(
        "cpuid"
        : "+a"(eax),
          "=b"(ebx),
          "=c"(ecx),
          "=d"(edx)
        : "c"(0U)
        : "memory"
    );

    uint64_t nowe_flagi = 0;

    if ((ebx & CPUID_SMEP_BIT) != 0) {
        nowe_flagi |= CR4_SMEP;
    }
    if ((ebx & CPUID_SMAP_BIT) != 0) {
        nowe_flagi |= CR4_SMAP;
    }

    if (nowe_flagi == 0) {
        return;
    }

    uint64_t cr4 = 0;
    asm volatile(
        "mov %%cr4, %0"
        : "=r"(cr4)
        :
        : "memory"
    );

    cr4 |= nowe_flagi;

    asm volatile(
        "mov %0, %%cr4"
        :
        : "r"(cr4)
        : "memory"
    );

    /* Potwierdzamy odczytem, ze sprzet przyjal oba bity. */
    uint64_t po = 0;
    asm volatile(
        "mov %%cr4, %0"
        : "=r"(po)
        :
        : "memory"
    );

    smep_aktywny =
        (po & CR4_SMEP) != 0;
    smap_aktywny =
        (po & CR4_SMAP) != 0;
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
        (adres &
         (ROZMIAR_STRONY_4K - 1ULL)) == 0 &&
        CzyZakresWMapowaniuFizycznym(
            adres,
            ROZMIAR_STRONY_4K
        );
}

static uint64_t fizyczny_adres_tabeli(
    const uint64_t* tabela
) {
    uint64_t fiz = 0;

    if (!WirtualnyDoFizycznego(
            tabela,
            &fiz) ||
        !tabela_fizyczna_bezposrednio_dostepna(
            fiz)) {
        return 0;
    }

    return fiz;
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

    uint64_t* tabela =
        static_cast<uint64_t*>(
            FizycznyZakresDoWirtualnego(
                fiz,
                ROZMIAR_STRONY_4K
            )
        );

    if (!tabela) {
        ZwolnijRamke(ramka);
        return nullptr;
    }

    WyzerujStrone(
        tabela
    );

    return tabela;
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

    return static_cast<uint64_t*>(
        FizycznyZakresDoWirtualnego(
            fiz,
            ROZMIAR_STRONY_4K
        )
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

    const uint64_t nowy_pd_fiz =
        fizyczny_adres_tabeli(nowy_pd);

    if (nowy_pd_fiz == 0) {
        return false;
    }

    pdp[indeks] =
        (nowy_pd_fiz &
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

    const uint64_t nowy_pt_fiz =
        fizyczny_adres_tabeli(nowy_pt);

    if (nowy_pt_fiz == 0) {
        return false;
    }

    pd[indeks] =
        (nowy_pt_fiz &
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

        const uint64_t nowa_fiz =
            fizyczny_adres_tabeli(nowa);

        if (nowa_fiz == 0) return false;

        wpis =
            (nowa_fiz &
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

        const uint64_t nowa_fiz =
            fizyczny_adres_tabeli(nowa);

        if (nowa_fiz == 0) return false;

        wpis =
            (nowa_fiz &
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

        const uint64_t nowa_fiz =
            fizyczny_adres_tabeli(nowa);

        if (nowa_fiz == 0) return false;

        wpis =
            (nowa_fiz &
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
    uint64_t flagi
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
        (flagi & PTE_USER) != 0;

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
        flagi &
        MASKA_FLAG_LISCIA_4K;

    if (user && (flagi_liscia & PTE_WRITE) && nx_aktywny) flagi_liscia |= PTE_NX;

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

static bool odmapuj_wspoldzielona_wewnetrznie(
    uint64_t* pml4,
    uint64_t adres_wirtualny
) {
    if (!pml4 || !adres_strony_poprawny(adres_wirtualny) ||
        !adres_user_kanoniczny(adres_wirtualny) ||
        adres_wirtualny < ROZMIAR_STRONY_4K) return false;

    const uint64_t e4 = pml4[indeks_pml4(adres_wirtualny)];
    if ((e4 & (PTE_PRESENT | PTE_USER)) != (PTE_PRESENT | PTE_USER))
        return false;
    uint64_t* pdp = tabela_z_wpisu(e4);
    if (!pdp) return false;
    const uint64_t e3 = pdp[indeks_pdp(adres_wirtualny)];
    if ((e3 & (PTE_PRESENT | PTE_USER)) != (PTE_PRESENT | PTE_USER) ||
        (e3 & PDE_PS) != 0) return false;
    uint64_t* pd = tabela_z_wpisu(e3);
    if (!pd) return false;
    const uint64_t e2 = pd[indeks_pd(adres_wirtualny)];
    if ((e2 & (PTE_PRESENT | PTE_USER)) != (PTE_PRESENT | PTE_USER) ||
        (e2 & PDE_PS) != 0) return false;
    uint64_t* pt = tabela_z_wpisu(e2);
    if (!pt) return false;
    uint64_t& e1 = pt[indeks_pt(adres_wirtualny)];
    if ((e1 & (PTE_PRESENT | PTE_USER | PTE_SHARED)) !=
        (PTE_PRESENT | PTE_USER | PTE_SHARED)) return false;
    e1 = 0;
    uniewaznij_tlb(adres_wirtualny);
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

            /*
             * sklonuj_hierarchie() kopiuje wysokie wpisy PML4 przez
             * wartosc. Ich poddrzewa naleza do jadra i sa wspoldzielone
             * przez wszystkie CR3. Zwijanie nieudanej kopii nie moze
             * zwracac ich tablic do PMM.
             */
            if (poziom == 4 && pml4_wpis_jadra(i)) {
                continue;
            }

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
        reinterpret_cast<void*>(
            fizyczny_adres_tabeli(tabela)
        )
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
            kopia[i] = 0;
            continue;
        }

        /*
         * Na poziomie PML4 wpisy dolnej polowy kanonicznej (0..255) sa
         * prywatne dla procesu i wymagaja sklonowania hierarchii. Wpisy
         * gornej polowy (256..511) sa wspoldzielonymi strukturami jadra
         * i sa kopiowane bezposrednio.
         */
        if (poziom == 4 && pml4_wpis_jadra(i)) {
            kopia[i] = wpis;
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
            static_cast<const uint64_t*>(
                FizycznyZakresDoWirtualnego(
                    fiz_dziecka,
                    ROZMIAR_STRONY_4K
                )
            );

        if (!stare_dziecko) {
            zwolnij_sklonowane_tabele_bez_lisci(
                kopia,
                poziom
            );
            return nullptr;
        }

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

        const uint64_t nowe_dziecko_fiz =
            fizyczny_adres_tabeli(
                nowe_dziecko
            );

        if (nowe_dziecko_fiz == 0) {
            zwolnij_sklonowane_tabele_bez_lisci(
                kopia,
                poziom
            );
            return nullptr;
        }

        kopia[i] =
            (nowe_dziecko_fiz &
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

            /* Lifetime ramek SHM prowadzi tabela obiektow BWS. */
            if ((wpis & PTE_SHARED) != 0) {
                tabela[i] = 0;
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

        ZwolnijRamke(
            reinterpret_cast<void*>(
                fizyczny_adres_tabeli(tabela)
            )
        );
        return;
    }

    for (size_t i = 0;
         i < 512;
         ++i) {

        /*
         * Cala dolna polowa PML4[0..255] jest prywatna dla procesu.
         * PML4[256..511] to wspoldzielone poddrzewa jadra (HHDM, heap,
         * grafika itd.) i nigdy nie wolno po nich rekurencyjnie zwalniac.
         */
        if (poziom == 4 && pml4_wpis_jadra(i)) {
            continue;
        }

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

    ZwolnijRamke(
        reinterpret_cast<void*>(
            fizyczny_adres_tabeli(tabela)
        )
    );
}

/* =========================================================================
 * BUDOWA BAZOWEJ PRZESTRZENI JADRA
 * ========================================================================= */

static bool przygotuj_mapowania_jadra() {
    WyzerujStrone(st_pml4);
    WyzerujStrone(st_pdp);
    WyzerujStrone(st_hhdm_pdp);

    for (size_t i = 0;
         i < 4;
         ++i) {
        WyzerujStrone(st_pd[i]);
        WyzerujStrone(st_hhdm_pd[i]);
    }

    for (size_t i = 0;
         i < MAKS_CZESCIOWYCH_PT_HHDM;
         ++i) {
        WyzerujStrone(st_hhdm_pt[i]);
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

    /*
     * HHDM posiada osobne PDP/PD/PT. Kazdy lisc powstaje dopiero po
     * potwierdzeniu w PMM, ze caly odwzorowywany zakres jest zwyklym RAM-em.
     * Dzieki temu LAPIC, IOAPIC, HPET, BAR-y PCI i LFB nie otrzymuja aliasu
     * WB pod 0xFFFF800000000000.
     */
    const uint64_t flagi_hhdm_liscia =
        PTE_PRESENT |
        PTE_WRITE |
        (nx_aktywny ? PTE_NX : 0ULL);

    bool znaleziono_ram = false;
    size_t uzyte_pt_hhdm = 0;

    for (uint64_t gigabajt = 0;
         gigabajt < 4;
         ++gigabajt) {

        bool pd_ma_wpis = false;

        for (uint64_t i = 0;
             i < 512;
             ++i) {

            const uint64_t fiz =
                gigabajt *
                    ROZMIAR_STRONY_1G +
                i *
                    ROZMIAR_STRONY_2M;

            if (CzyZakresWMapowaniuFizycznym(
                    fiz,
                    ROZMIAR_STRONY_2M)) {

                st_hhdm_pd[gigabajt][i] =
                    (fiz & MASKA_ADRESU_2M) |
                    flagi_hhdm_liscia |
                    PDE_PS;

                pd_ma_wpis = true;
                znaleziono_ram = true;
                continue;
            }

            bool pt_ma_wpis = false;

            for (uint64_t j = 0;
                 j < 512;
                 ++j) {

                const uint64_t strona =
                    fiz +
                    j * ROZMIAR_STRONY_4K;

                if (!CzyZakresWMapowaniuFizycznym(
                        strona,
                        ROZMIAR_STRONY_4K)) {
                    continue;
                }

                if (uzyte_pt_hhdm >=
                    MAKS_CZESCIOWYCH_PT_HHDM) {
                    return false;
                }

                st_hhdm_pt[uzyte_pt_hhdm][j] =
                    (strona & MASKA_ADRESU_4K) |
                    flagi_hhdm_liscia;

                pt_ma_wpis = true;
                znaleziono_ram = true;
            }

            if (pt_ma_wpis) {
                st_hhdm_pd[gigabajt][i] =
                    (reinterpret_cast<uint64_t>(
                        st_hhdm_pt[uzyte_pt_hhdm]) &
                     MASKA_ADRESU_4K) |
                    PTE_PRESENT |
                    PTE_WRITE;

                ++uzyte_pt_hhdm;
                pd_ma_wpis = true;
            }
        }

        if (pd_ma_wpis) {
            st_hhdm_pdp[gigabajt] =
                (reinterpret_cast<uint64_t>(
                    st_hhdm_pd[gigabajt]) &
                 MASKA_ADRESU_4K) |
                PTE_PRESENT |
                PTE_WRITE;
        }
    }

    if (!znaleziono_ram) {
        return false;
    }

    st_pml4[256] =
        (reinterpret_cast<uint64_t>(
            st_hhdm_pdp) &
         MASKA_ADRESU_4K) |
        PTE_PRESENT |
        PTE_WRITE;

    return true;
}

} // namespace

/* =========================================================================
 * PUBLICZNY STAN NX
 * ========================================================================= */

/*
 * Warstwy, ktore publikuja strony Ring 3 (sterta BWS, SHM, loader), musza
 * wiedziec, czy sprzet obsluguje bit NX. Bez tego nie da sie uczciwie
 * zdecydowac, czy strona danych ma byc niewykonywalna.
 */
extern "C" bool vmm_nx_aktywny() {
    return nx_aktywny;
}

extern "C" bool vmm_smep_aktywny() {
    return smep_aktywny;
}

extern "C" bool vmm_smap_aktywny() {
    return smap_aktywny;
}

extern "C" int& vmm_okno_uzytkownika_glebokosc() {
    /*
     * Licznik zagniezdzen okien SMAP. Jednordzeniowe jadro nie jest
     * wywlaszczane w trakcie obslugi BWS, wiec zwykly int wystarcza.
     */
    static int glebokosc = 0;
    return glebokosc;
}

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

bool ZmapujStroneSprawdzona(
    void* adres_wirtualny,
    void* adres_fizyczny,
    uint32_t flagi
) {
    BlokadaVMM blokada;

    if (!vmm_zainicjalizowany) {
        return false;
    }

    uint64_t* aktualne =
        pml4_z_cr3();

    if (!aktualne) {
        return false;
    }

    globalne_pml4 =
        aktualne;

    return mapuj_strone_wewnetrznie(
        aktualne,
        reinterpret_cast<uint64_t>(
            adres_wirtualny),
        reinterpret_cast<uint64_t>(
            adres_fizyczny),
        flagi
    );
}

extern "C" void SerialLog(const char*);
void ZmapujStrone(void* va,void* pa,uint32_t flags) {
    (void)ZmapujStroneSprawdzona(va,pa,flags);
}

bool ZmapujStrone64Sprawdzona(
    void* adres_wirtualny,
    void* adres_fizyczny,
    uint64_t flagi
) {
    BlokadaVMM blokada;

    if (!vmm_zainicjalizowany) {
        return false;
    }

    uint64_t* aktualne =
        pml4_z_cr3();

    if (!aktualne) {
        return false;
    }

    globalne_pml4 =
        aktualne;

    return mapuj_strone_wewnetrznie(
        aktualne,
        reinterpret_cast<uint64_t>(
            adres_wirtualny),
        reinterpret_cast<uint64_t>(
            adres_fizyczny),
        flagi
    );
}

static bool vmm_mapuj_nowa_strone_procesu_wewnetrznie(
    uint64_t* pml4,
    void* va,
    void* pa,
    bool zapis,
    bool wykonuj
) {
    const uint64_t adres = reinterpret_cast<uint64_t>(va);
    if (!vmm_zainicjalizowany || !nx_aktywny || (zapis && wykonuj) || !pml4 || !pa || adres < RING3_BASE ||
        !adres_user_kanoniczny(adres) || (adres & 4095U) ||
        (reinterpret_cast<uint64_t>(pa) & 4095U)) return false;
    const auto obecne = sprawdz_mapowanie(pml4, adres);
    /* Powyzej identity-mapy 4 GiB w przestrzeni procesu nie ma niczego, co wolno zastapic: obecna strona
     * jadra (okna zarezerwowane_va.h, klonowane z dolnej polowy) NIE moze zostac nadpisana strona
     * uzytkownika (jadro pisaloby/czytalo wtedy pamiec procesu). Identity-mapa < 4 GiB nadal jest dzielona. */
    if (!VmmZakres::wolno_mapowac_strone_uzytkownika(obecne.obecna, obecne.user, adres)) return false;

    uint64_t flagi = VMM_FLAGA_OBECNA | VMM_FLAGA_USER;
    if (zapis) flagi |= VMM_FLAGA_ZAPIS;

    /*
     * Strona Ring 3 jest wykonywalna tylko wtedy, gdy wywolujacy jawnie
     * o to poprosil (segment kodu). Wszystkie pozostale mapowania uzyskuja
     * NX, o ile sprzet obsluguje bit 63 - inaczej W^X bylby pozorny.
     */
    if (!wykonuj && nx_aktywny) {
        flagi |= VMM_FLAGA_NX;
    }

    return mapuj_strone_wewnetrznie(pml4, adres,
        reinterpret_cast<uint64_t>(pa), flagi);
}

/* Czy w BAZOWEJ przestrzeni jadra (zrodlo klonow procesow) zakres [start,koniec) jest calkowicie niezmapowany?
 * ASLR loadera losuje ponownie, gdy nie - niezaleznie od recznej listy zarezerwowane_va.h. */
extern "C" bool VmmZakresWolnyWBazie(uint64_t start, uint64_t koniec) {
    BlokadaVMM blokada;
    if (!vmm_zainicjalizowany || !bazowe_pml4_jadra) return false;
    return VmmZakres::zakres_wolny(bazowe_pml4_jadra, start, koniec,
        [](uint64_t wpis) -> const uint64_t* {
            if (!tabela_fizyczna_bezposrednio_dostepna(wpis & MASKA_ADRESU_4K)) return nullptr;
            return static_cast<const uint64_t*>(FizycznyZakresDoWirtualnego(wpis & MASKA_ADRESU_4K, ROZMIAR_STRONY_4K));
        });
}

extern "C" bool VmmNxGotowy() { return nx_aktywny; }
extern "C" bool VmmMapujStroneBurV2WPrzestrzeni(void* root, void* va, void* pa, uint64_t flags) {
    BlokadaVMM blokada;
    if (flags & ~(PTE_WRITE | PTE_NX)) return false;
    return vmm_mapuj_nowa_strone_procesu_wewnetrznie(
        static_cast<uint64_t*>(root), va, pa,
        (flags & PTE_WRITE) != 0, (flags & PTE_NX) == 0);
}
extern "C" bool VmmMapujStroneBurV2(void* va, void* pa, uint64_t flags) {
    return VmmMapujStroneBurV2WPrzestrzeni(pml4_z_cr3(), va, pa, flags);
}

extern "C" bool VmmMapujNowaStroneProcesu(
    void* va,
    void* pa,
    bool zapis,
    bool wykonuj
) {
    BlokadaVMM blokada;
    return vmm_mapuj_nowa_strone_procesu_wewnetrznie(
        pml4_z_cr3(), va, pa, zapis, wykonuj);
}

extern "C" bool VmmMapujNowaStroneWPrzestrzeniProcesu(
    void* pml4,
    void* va,
    void* pa,
    bool zapis,
    bool wykonuj
) {
    BlokadaVMM blokada;
    return vmm_mapuj_nowa_strone_procesu_wewnetrznie(
        static_cast<uint64_t*>(pml4), va, pa, zapis, wykonuj);
}

extern "C" bool VmmMapujNowaStroneJadra(void* va, void* pa) {
    BlokadaVMM blokada;
    if (!vmm_zainicjalizowany || !va || !pa ||
        (reinterpret_cast<uint64_t>(va) & 4095U) ||
        (reinterpret_cast<uint64_t>(pa) & 4095U)) return false;
    uint64_t* pml4 = pml4_z_cr3();
    if (!pml4 || sprawdz_mapowanie(pml4, reinterpret_cast<uint64_t>(va)).obecna) return false;
    return mapuj_strone_wewnetrznie(pml4, reinterpret_cast<uint64_t>(va),
        reinterpret_cast<uint64_t>(pa), VMM_FLAGA_OBECNA | VMM_FLAGA_ZAPIS);
}

extern "C" bool VmmMapujStroneWspoldzielona(
    void* adres_wirtualny,
    void* adres_fizyczny,
    bool zapis
) {
    BlokadaVMM blokada;
    if (!vmm_zainicjalizowany) return false;
    uint64_t* aktualne = pml4_z_cr3();
    if (!aktualne) return false;
    const uint64_t va = reinterpret_cast<uint64_t>(adres_wirtualny);
    const uint64_t pa = reinterpret_cast<uint64_t>(adres_fizyczny);
    if (sprawdz_mapowanie(aktualne, va).obecna) return false;
    uint64_t flagi = VMM_FLAGA_OBECNA | VMM_FLAGA_USER |
                     VMM_FLAGA_WSPOLDZIELONA;
    if (zapis) flagi |= VMM_FLAGA_ZAPIS;
    /* Pamiec wspoldzielona jest danymi, nigdy kodem. */
    if (nx_aktywny) flagi |= VMM_FLAGA_NX;
    return mapuj_strone_wewnetrznie(aktualne, va, pa, flagi);
}

extern "C" bool VmmOdmapujStroneWspoldzielona(
    void* adres_wirtualny
) {
    BlokadaVMM blokada;
    if (!vmm_zainicjalizowany) return false;
    uint64_t* aktualne = pml4_z_cr3();
    return aktualne && odmapuj_wspoldzielona_wewnetrznie(
        aktualne, reinterpret_cast<uint64_t>(adres_wirtualny));
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

    wlacz_nx_jezeli_obslugiwane();

    wlacz_smep_smap_jezeli_obslugiwane();

    if (!przygotuj_mapowania_jadra()) {
        return;
    }

    bazowe_pml4_jadra =
        static_cast<uint64_t*>(
            FizycznyZakresDoWirtualnego(
                reinterpret_cast<uint64_t>(st_pml4),
                ROZMIAR_STRONY_4K
            )
        );

    if (!bazowe_pml4_jadra) {
        return;
    }

    globalne_pml4 =
        bazowe_pml4_jadra;

    /*
     * Wszystkie tablice statyczne znajduja sie w obrazie jadra, ktory
     * boot.S mapuje przed wejsciem tutaj.
     */
    zapisz_cr3(
        bazowe_pml4_jadra
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

    uint64_t adres = 0;

    if (!WirtualnyDoFizycznego(
            pml4,
            &adres)) {
        return;
    }

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

    uint64_t fiz = 0;

    if (!WirtualnyDoFizycznego(
            do_zniszczenia,
            &fiz)) {
        return;
    }

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

#include "bufory_grafiki.h"
extern "C" void* VmmOdmapujBuforGrafiki(void* adres) {
    const uint64_t va=reinterpret_cast<uint64_t>(adres);
    if(va<BUFORY_GRAFIKI_BAZA || va>=BUFORY_GRAFIKI_BAZA+uint64_t(BUFORY_GRAFIKI_SLOTOW)*BUFORY_GRAFIKI_SLOT || (va&4095)) return nullptr;
    BlokadaVMM guard;
    uint64_t* tab=pml4_z_cr3();
    const unsigned indeksy[4]={unsigned(indeks_pml4(va)),unsigned(indeks_pdp(va)),unsigned(indeks_pd(va)),unsigned(indeks_pt(va))};
    for(unsigned i=0;i<3;++i) {
        if(!tab) return nullptr;
        const uint64_t e=tab[indeksy[i]];
        if(!(e&PTE_PRESENT)||(e&(PTE_USER|PDE_PS))) return nullptr;
        tab=tabela_z_wpisu(e);
    }
    if(!tab) return nullptr;
    uint64_t& e=tab[indeksy[3]];
    if(!(e&PTE_PRESENT)||(e&PTE_USER)) return nullptr;
    void* pa=reinterpret_cast<void*>(e&MASKA_ADRESU_4K);
    e=0; uniewaznij_tlb(va);
    // Puste PT pozostaja wspolne; ich liczba jest ograniczona rozmiarem puli.
    return pa;
}

extern "C" void* VmmOdmapujStroneJadra(void* adres) {
    const uint64_t va = reinterpret_cast<uint64_t>(adres);
    if (!adres_strony_poprawny(va)) return nullptr;
    BlokadaVMM guard;
    uint64_t* tab = pml4_z_cr3();
    const unsigned indeksy[4] = {
        unsigned(indeks_pml4(va)),
        unsigned(indeks_pdp(va)),
        unsigned(indeks_pd(va)),
        unsigned(indeks_pt(va))
    };
    for (unsigned i = 0; i < 3; ++i) {
        if (!tab) return nullptr;
        const uint64_t e = tab[indeksy[i]];
        if (!(e & PTE_PRESENT) || (e & (PTE_USER | PDE_PS))) return nullptr;
        tab = tabela_z_wpisu(e);
    }
    if (!tab) return nullptr;
    uint64_t& e = tab[indeksy[3]];
    if (!(e & PTE_PRESENT) || (e & PTE_USER)) return nullptr;
    void* pa = reinterpret_cast<void*>(e & MASKA_ADRESU_4K);
    e = 0;
    uniewaznij_tlb(va);
    return pa;
}

/*
 * Diagnostyczny, read-only odczyt wpisu tablicy stron dla obslugi #PF.
 *
 * Wywolywane wylacznie ze sciezki panic_wyjatek (IF=0, system bedzie
 * zatrzymany). Celowo NIE pobiera BlokadaVMM: przerwany kontekst mogl
 * trzymac te blokade (np. blad w mutatorze VMM), a zakleszczenie w
 * diagnostyce byloby gorsze niz odczyt potencjalnie przejsciowego wpisu.
 * Wynik sluzy wylacznie potwierdzeniu bitu Present; decyzje podejmuja
 * zakres guard page (niezmiennik) oraz bit P kodu bledu #PF.
 */
extern "C" bool VmmDiagnostykaWpisu(uint64_t va, uint64_t* out_wpis) {
    if (out_wpis) {
        *out_wpis = 0;
    }

    uint64_t* pml4 = pml4_z_cr3();
    if (!pml4) {
        return false;
    }

    const WynikWalkera wynik = sprawdz_mapowanie(pml4, va);
    if (out_wpis) {
        *out_wpis = wynik.wpis;
    }

    return wynik.obecna;
}

extern "C" bool VmmTestujWspoldzieloneMapowanieJadra(const void* adres) {    const uint64_t va = reinterpret_cast<uint64_t>(adres);
    if (!adres || !pml4_wpis_jadra(indeks_pml4(va))) return false;

    BlokadaVMM guard;
    if (!vmm_zainicjalizowany || !bazowe_pml4_jadra) return false;

    const WynikWalkera przed = sprawdz_mapowanie(bazowe_pml4_jadra, va);
    if (!przed.obecna || przed.user || !przed.zapisywalna) return false;

    constexpr size_t LICZBA_MAPOWAN_TESTU = 8;
    constexpr size_t INDEKS_SHM = LICZBA_MAPOWAN_TESTU - 1;
    const uint64_t adresy[LICZBA_MAPOWAN_TESTU] = {
        RING3_DEFAULT_TEXT_VA,
        RING3_DEFAULT_DATA_VA,
        RING3_HEAP_BASE,
        RING3_STACK_TOP,
        RING3_STACK_TOP + ROZMIAR_STRONY_4K,
        RING3_STACK_TOP + 2 * ROZMIAR_STRONY_4K,
        RING3_STACK_TOP + 3 * ROZMIAR_STRONY_4K,
        RING3_SHM_BASE
    };

    if (indeks_pml4(RING3_DEFAULT_TEXT_VA) != 0 ||
        indeks_pml4(RING3_DEFAULT_DATA_VA) != 0 ||
        indeks_pml4(RING3_HEAP_BASE) != 0 ||
        indeks_pml4(RING3_SHM_BASE) != 0 ||
        indeks_pml4(RING3_STACK_TOP) != 255) {
        return false;
    }

    const uint64_t wolne_stabilne = pmm_pobierz_wolne_ramki();

    for (size_t proba = 0; proba < 100; ++proba) {
        uint64_t* proces = sklonuj_hierarchie(bazowe_pml4_jadra, 4);
        if (!proces) return false;

        void* ramki[LICZBA_MAPOWAN_TESTU]{};
        bool zmapowane[LICZBA_MAPOWAN_TESTU]{};
        bool poprawne = true;

        for (size_t i = 0; i < LICZBA_MAPOWAN_TESTU; ++i) {
            ramki[i] = ZaalokujRamke();
            if (!ramki[i]) {
                poprawne = false;
                break;
            }

            uint32_t flagi = VMM_FLAGA_OBECNA | VMM_FLAGA_USER;
            if (i != 0) flagi |= VMM_FLAGA_ZAPIS;
            if (i == INDEKS_SHM) flagi |= VMM_FLAGA_WSPOLDZIELONA;

            zmapowane[i] = mapuj_strone_wewnetrznie(
                proces,
                adresy[i],
                reinterpret_cast<uint64_t>(ramki[i]),
                flagi
            );
            if (!zmapowane[i]) {
                poprawne = false;
                break;
            }

            const WynikWalkera wpis = sprawdz_mapowanie(proces, adresy[i]);
            if (!wpis.obecna || !wpis.user ||
                (i != 0 && !wpis.zapisywalna) ||
                ((wpis.wpis & MASKA_ADRESU_4K) !=
                 (reinterpret_cast<uint64_t>(ramki[i]) & MASKA_ADRESU_4K))) {
                poprawne = false;
                break;
            }
        }

        const WynikWalkera kopia = sprawdz_mapowanie(proces, va);
        if (!kopia.obecna || kopia.user || !kopia.zapisywalna ||
            kopia.wpis != przed.wpis) {
            poprawne = false;
        }

        zniszcz_hierarchie_procesu(proces, 4);

        for (size_t i = 0; i < LICZBA_MAPOWAN_TESTU; ++i) {
            if (!ramki[i]) continue;

            /*
             * Prywatne, zmapowane liscie zwolnil teardown. Ramka SHM
             * nalezy do obiektu wspoldzielonego, a ramka bez mapowania
             * nadal nalezy do tej funkcji testowej.
             */
            if (!zmapowane[i] || i == INDEKS_SHM) {
                ZwolnijRamke(ramki[i]);
            }
        }

        const WynikWalkera po = sprawdz_mapowanie(bazowe_pml4_jadra, va);
        if (!poprawne || !po.obecna || po.user || !po.zapisywalna ||
            po.wpis != przed.wpis ||
            pmm_pobierz_wolne_ramki() != wolne_stabilne) {
            return false;
        }
    }

    return true;
}
