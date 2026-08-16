/*
 * Bursztyn OS - Global Descriptor Table (GDT) dla x86_64 Long Mode
 *
 * Uklad selektorow jest czescia ABI jadra i MUSI pozostac zgodny z:
 *
 *   przerwania.S
 *   ring3.S
 *   syscall.S / syscalls.cpp
 *   tss.cpp
 *
 * Uklad:
 *
 *   0x00  Null
 *   0x08  Kernel Code, Ring 0
 *   0x10  Kernel Data, Ring 0
 *   0x18  User Data,   Ring 3  -> selektor z RPL3: 0x1B
 *   0x20  User Code,   Ring 3  -> selektor z RPL3: 0x23
 *   0x28  64-bit Available TSS -> zajmuje 16 bajtow / dwa sloty GDT
 *
 * Dla SYSRET:
 *
 *   STAR[63:48] = 0x13
 *
 * daje:
 *
 *   SS = 0x13 + 8  | 3 = 0x1B
 *   CS = 0x13 + 16 | 3 = 0x23
 *
 * Najwazniejsze poprawki wzgledem starej wersji:
 *
 *   - TSS jest prawdziwym 16-bajtowym deskryptorem systemowym,
 *   - segmenty danych NIE maja ustawionego bitu L (Long),
 *   - kod 64-bit ma L=1 i D/B=0,
 *   - wszystkie rozmiary i offsety struktur sa kontrolowane static_assert,
 *   - GDT jest budowana w nieaktywnej kopii, a dopiero potem publikowana
 *     przez LGDT; ponowna inicjalizacja nie rozrywa aktywnej tabeli,
 *   - cala przebudowa jest wykonywana przy lokalnie wylaczonych IRQ,
 *   - po LGDT sprawdzamy GDTR oraz CS/DS/ES/SS,
 *   - bledny stan zatrzymuje CPU fail-closed przed dalszym startem kernela.
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* =========================================================================
 * 1. ABI ZEWNĘTRZNE
 * ========================================================================= */

/*
 * Implementacja znajduje sie w przerwania.S.
 * Argument:
 *   RDI = adres 10-bajtowego pseudo-deskryptora GDTR.
 *
 * Funkcja wykonuje:
 *   LGDT
 *   reload DS/ES/SS
 *   far return do nowego CS
 * i zachowuje poprzedni stan IF.
 */
extern "C" void zaladuj_zaktualizowane_gdt(
    uint64_t adres_gdtr
);

/*
 * TSS jest zdefiniowany w tss.cpp.
 *
 * Nie potrzebujemy tutaj jego pelnej definicji - do deskryptora GDT
 * potrzebny jest tylko adres obiektu. Rozmiar sprzetowego TSS x86_64
 * jest stale 104 bajty i jest dodatkowo static_assertowany w tss.cpp.
 */
struct tss_wpis;
extern tss_wpis globalny_tss;

/* =========================================================================
 * 2. STALE ABI SELEKTOROW
 * ========================================================================= */

namespace {

constexpr uint16_t SELEKTOR_NULL =
    0x00U;

constexpr uint16_t SELEKTOR_KODU_JADRA =
    0x08U;

constexpr uint16_t SELEKTOR_DANYCH_JADRA =
    0x10U;

constexpr uint16_t SELEKTOR_DANYCH_USER =
    0x18U;

constexpr uint16_t SELEKTOR_KODU_USER =
    0x20U;

constexpr uint16_t SELEKTOR_TSS =
    0x28U;

constexpr uint16_t RPL_RING3 =
    0x0003U;

constexpr uint16_t SELEKTOR_DANYCH_USER_R3 =
    SELEKTOR_DANYCH_USER |
    RPL_RING3;

constexpr uint16_t SELEKTOR_KODU_USER_R3 =
    SELEKTOR_KODU_USER |
    RPL_RING3;

/*
 * Wartość bazowa używana przez MSR STAR w syscalls.cpp.
 */
constexpr uint16_t STAR_USER_BASE =
    0x13U;

static_assert(
    SELEKTOR_KODU_JADRA == 0x08U,
    "ABI przerwania.S wymaga Kernel CS = 0x08"
);

static_assert(
    SELEKTOR_DANYCH_JADRA == 0x10U,
    "ABI przerwania.S wymaga Kernel DS = 0x10"
);

static_assert(
    SELEKTOR_DANYCH_USER_R3 == 0x1BU,
    "ABI Ring3 wymaga User SS/DS = 0x1B"
);

static_assert(
    SELEKTOR_KODU_USER_R3 == 0x23U,
    "ABI Ring3 wymaga User CS = 0x23"
);

static_assert(
    static_cast<uint16_t>(
        STAR_USER_BASE + 8U
    ) == SELEKTOR_DANYCH_USER_R3,
    "STAR musi generowac User SS = 0x1B"
);

static_assert(
    static_cast<uint16_t>(
        STAR_USER_BASE + 16U
    ) == SELEKTOR_KODU_USER_R3,
    "STAR musi generowac User CS = 0x23"
);

static_assert(
    SELEKTOR_TSS == 0x28U,
    "tss.cpp i kernel wymagaja selektora TSS = 0x28"
);

/* =========================================================================
 * 3. STALE DESKRYPTOROW
 * ========================================================================= */

/*
 * 20-bitowy limit + G=1 daje klasyczne 4 GiB - 1.
 * W Long Mode baza/limit kodu i danych sa w praktyce ignorowane,
 * ale poprawne atrybuty deskryptora nadal maja znaczenie przy ladowaniu
 * selektorow i IRETQ.
 */
constexpr uint32_t LIMIT_SEGMENTU_PLASKIEGO =
    0x000FFFFFU;

/*
 * Access byte:
 *
 * bit 7     P
 * bits 6:5  DPL
 * bit 4     S
 * bits 3:0  Type
 */
constexpr uint8_t DOSTEP_KERNEL_CODE =
    0x9AU; /* P=1 DPL0 S=1 Executable Readable */

constexpr uint8_t DOSTEP_KERNEL_DATA =
    0x92U; /* P=1 DPL0 S=1 Data Read/Write */

constexpr uint8_t DOSTEP_USER_CODE =
    0xFAU; /* P=1 DPL3 S=1 Executable Readable */

constexpr uint8_t DOSTEP_USER_DATA =
    0xF2U; /* P=1 DPL3 S=1 Data Read/Write */

/*
 * Flags - gorna polowa byte limit_hi_flags:
 *
 *   bit 7 G
 *   bit 6 D/B
 *   bit 5 L
 *   bit 4 AVL
 *
 * 64-bit code:
 *   G=1, D/B=0, L=1 -> 0xA0
 *
 * Data:
 *   G=1, D/B=1, L=0 -> 0xC0
 *
 * Stara wersja przekazywala 0xAF rowniez dla DATA, co ustawialo L=1
 * w deskryptorze danych. Bit L nie jest dla segmentu danych prawidlowym
 * atrybutem i nie powinien byc tam ustawiony.
 */
constexpr uint8_t FLAGI_CODE_64 =
    0xA0U;

constexpr uint8_t FLAGI_DATA =
    0xC0U;

/*
 * System descriptor:
 *
 *   P=1
 *   DPL=0
 *   S=0
 *   Type=1001b = Available 64-bit TSS
 */
constexpr uint8_t DOSTEP_TSS_AVAILABLE_64 =
    0x89U;

/*
 * Sprzetowy x86_64 TSS ma 0x68 = 104 bajty.
 * Deskryptor przechowuje limit = rozmiar - 1.
 */
constexpr uint32_t ROZMIAR_TSS_X86_64 =
    0x68U;

constexpr uint32_t LIMIT_TSS_X86_64 =
    ROZMIAR_TSS_X86_64 -
    1U;

static_assert(
    ROZMIAR_TSS_X86_64 == 104U,
    "x86_64 TSS musi miec 104 bajty"
);

static_assert(
    LIMIT_TSS_X86_64 == 103U,
    "Limit TSS powinien wynosic 103"
);

/* =========================================================================
 * 4. SPRZETOWE STRUKTURY GDT
 * ========================================================================= */

/*
 * Standardowy 8-bajtowy deskryptor kodu/danych.
 */
struct DeskryptorGDT {
    uint16_t limit_dolny;
    uint16_t baza_dolna;
    uint8_t baza_srodkowa;
    uint8_t dostep;
    uint8_t limit_gorny_flagi;
    uint8_t baza_gorna;
} __attribute__((packed));

/*
 * 16-bajtowy deskryptor systemowy TSS w Long Mode.
 *
 * Pierwsze 8 bajtow maja format podobny do deskryptora systemowego,
 * kolejne 8 przechowuja bity 63:32 bazy oraz zarezerwowane zero.
 */
struct DeskryptorTSS64 {
    uint16_t limit_dolny;
    uint16_t baza_15_0;
    uint8_t baza_23_16;
    uint8_t dostep;
    uint8_t limit_gorny_flagi;
    uint8_t baza_31_24;

    uint32_t baza_63_32;
    uint32_t zarezerwowane;
} __attribute__((packed));

/*
 * Pseudo-deskryptor instrukcji LGDT/SGDT w 64-bit mode.
 */
struct RejestrGDT {
    uint16_t rozmiar;
    uint64_t adres;
} __attribute__((packed));

/*
 * Fizyczny uklad tabeli:
 *
 *   5 * 8 B + 16 B = 56 B = 7 klasycznych slotow GDT.
 */
struct TablicaGDT {
    DeskryptorGDT null;
    DeskryptorGDT kernel_code;
    DeskryptorGDT kernel_data;
    DeskryptorGDT user_data;
    DeskryptorGDT user_code;
    DeskryptorTSS64 tss;
} __attribute__((packed));

/* =========================================================================
 * 5. KONTROLA ABI SPRZETOWEGO
 * ========================================================================= */

static_assert(
    sizeof(DeskryptorGDT) == 8U,
    "Deskryptor GDT kod/dane musi miec 8 bajtow"
);

static_assert(
    offsetof(DeskryptorGDT, dostep) == 5U,
    "Nieprawidlowy offset Access Byte w GDT"
);

static_assert(
    offsetof(DeskryptorGDT, limit_gorny_flagi) == 6U,
    "Nieprawidlowy offset Flags/Limit w GDT"
);

static_assert(
    sizeof(DeskryptorTSS64) == 16U,
    "Deskryptor TSS w x86_64 musi miec 16 bajtow"
);

static_assert(
    offsetof(DeskryptorTSS64, baza_63_32) == 8U,
    "Gorna czesc bazy TSS musi zaczynac sie od bajtu 8"
);

static_assert(
    offsetof(DeskryptorTSS64, zarezerwowane) == 12U,
    "Reserved TSS descriptor musi zaczynac sie od bajtu 12"
);

static_assert(
    sizeof(RejestrGDT) == 10U,
    "GDTR w Long Mode musi miec 10 bajtow"
);

static_assert(
    offsetof(RejestrGDT, adres) == 2U,
    "Adres GDTR musi zaczynac sie pod offsetem 2"
);

static_assert(
    sizeof(TablicaGDT) == 56U,
    "GDT Bursztyn OS musi zajmowac 7 slotow = 56 bajtow"
);

static_assert(
    offsetof(TablicaGDT, kernel_code) == 0x08U,
    "Kernel Code musi miec selektor 0x08"
);

static_assert(
    offsetof(TablicaGDT, kernel_data) == 0x10U,
    "Kernel Data musi miec selektor 0x10"
);

static_assert(
    offsetof(TablicaGDT, user_data) == 0x18U,
    "User Data musi miec selektor 0x18"
);

static_assert(
    offsetof(TablicaGDT, user_code) == 0x20U,
    "User Code musi miec selektor 0x20"
);

static_assert(
    offsetof(TablicaGDT, tss) == 0x28U,
    "TSS musi miec selektor 0x28"
);

/* =========================================================================
 * 6. PODWOJNY BUFOR GDT
 * ========================================================================= */

/*
 * Budujemy nowa tabele w kopii, ktora NIE jest aktualnie zaladowana.
 *
 * Jest to wazne przy ewentualnej ponownej inicjalizacji:
 * przerwanie/NMI w trakcie budowania nie zobaczy polowicznie zmienionych
 * deskryptorow aktywnej GDT.
 *
 * Sam moment LGDT jest atomowa zmiana GDTR z punktu widzenia kodu.
 */
alignas(16)
TablicaGDT tablice_gdt[2] = {};

/*
 * 0xFF oznacza, ze zadna z naszych dwoch tabel nie zostala jeszcze
 * opublikowana. Zmienna ma jawna inicjalizacje .data, nie polega na BSS.
 */
uint8_t aktywna_tablica_gdt =
    0xFFU;

/* =========================================================================
 * 7. HELPERY CPU
 * ========================================================================= */

struct StanPrzerwan {
    uint64_t rflags;
};

StanPrzerwan zapisz_i_wylacz_przerwania() {
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

void przywroc_przerwania(
    StanPrzerwan stan
) {
    if ((stan.rflags &
         (UINT64_C(1) << 9)) != 0) {

        asm volatile(
            "sti"
            :
            :
            : "memory"
        );
    }
}

[[noreturn]]
void gdt_zatrzymaj_cpu() {
    asm volatile(
        "cli"
        :
        :
        : "memory", "cc"
    );

    for (;;) {
        asm volatile(
            "hlt"
            :
            :
            : "memory"
        );
    }
}

bool adres_kanoniczny_48(
    uint64_t adres
) {
    const uint64_t gora =
        adres >> 48;

    const bool bit47 =
        (adres &
         (UINT64_C(1) << 47)) != 0;

    return
        bit47
            ? gora == UINT64_C(0xFFFF)
            : gora == 0;
}

/* =========================================================================
 * 8. BUDOWANIE DESKRYPTORA KOD/DANE
 * ========================================================================= */

DeskryptorGDT zbuduj_deskryptor_segmentu(
    uint64_t baza,
    uint32_t limit,
    uint8_t dostep,
    uint8_t flagi
) {
    DeskryptorGDT d{};

    d.limit_dolny =
        static_cast<uint16_t>(
            limit &
            0xFFFFU
        );

    d.baza_dolna =
        static_cast<uint16_t>(
            baza &
            UINT64_C(0xFFFF)
        );

    d.baza_srodkowa =
        static_cast<uint8_t>(
            (baza >> 16) &
            UINT64_C(0xFF)
        );

    d.dostep =
        dostep;

    d.limit_gorny_flagi =
        static_cast<uint8_t>(
            ((limit >> 16) &
             0x0FU) |
            (flagi &
             0xF0U)
        );

    d.baza_gorna =
        static_cast<uint8_t>(
            (baza >> 24) &
            UINT64_C(0xFF)
        );

    return d;
}

/* =========================================================================
 * 9. BUDOWANIE DESKRYPTORA TSS
 * ========================================================================= */

DeskryptorTSS64 zbuduj_deskryptor_tss(
    uint64_t baza,
    uint32_t limit
) {
    DeskryptorTSS64 d{};

    d.limit_dolny =
        static_cast<uint16_t>(
            limit &
            0xFFFFU
        );

    d.baza_15_0 =
        static_cast<uint16_t>(
            baza &
            UINT64_C(0xFFFF)
        );

    d.baza_23_16 =
        static_cast<uint8_t>(
            (baza >> 16) &
            UINT64_C(0xFF)
        );

    d.dostep =
        DOSTEP_TSS_AVAILABLE_64;

    /*
     * G=0 - limit TSS jest w bajtach.
     * D/B i L musza pozostac 0 dla tego deskryptora systemowego.
     */
    d.limit_gorny_flagi =
        static_cast<uint8_t>(
            (limit >> 16) &
            0x0FU
        );

    d.baza_31_24 =
        static_cast<uint8_t>(
            (baza >> 24) &
            UINT64_C(0xFF)
        );

    d.baza_63_32 =
        static_cast<uint32_t>(
            baza >> 32
        );

    d.zarezerwowane =
        0;

    return d;
}

/* =========================================================================
 * 10. BUDOWANIE CALEJ GDT
 * ========================================================================= */

void zbuduj_gdt(
    TablicaGDT* gdt
) {
    if (!gdt) {
        gdt_zatrzymaj_cpu();
    }

    const uint64_t baza_tss =
        reinterpret_cast<uint64_t>(
            &globalny_tss
        );

    if (!adres_kanoniczny_48(
            baza_tss) ||
        baza_tss >
            UINT64_MAX -
            LIMIT_TSS_X86_64 ||
        !adres_kanoniczny_48(
            baza_tss +
            LIMIT_TSS_X86_64)) {

        gdt_zatrzymaj_cpu();
    }

    /*
     * Null descriptor.
     */
    gdt->null =
        zbuduj_deskryptor_segmentu(
            0,
            0,
            0,
            0
        );

    /*
     * Ring 0.
     */
    gdt->kernel_code =
        zbuduj_deskryptor_segmentu(
            0,
            LIMIT_SEGMENTU_PLASKIEGO,
            DOSTEP_KERNEL_CODE,
            FLAGI_CODE_64
        );

    gdt->kernel_data =
        zbuduj_deskryptor_segmentu(
            0,
            LIMIT_SEGMENTU_PLASKIEGO,
            DOSTEP_KERNEL_DATA,
            FLAGI_DATA
        );

    /*
     * Ring 3.
     *
     * Kolejnosc User Data -> User Code jest czescia ABI SYSRET.
     */
    gdt->user_data =
        zbuduj_deskryptor_segmentu(
            0,
            LIMIT_SEGMENTU_PLASKIEGO,
            DOSTEP_USER_DATA,
            FLAGI_DATA
        );

    gdt->user_code =
        zbuduj_deskryptor_segmentu(
            0,
            LIMIT_SEGMENTU_PLASKIEGO,
            DOSTEP_USER_CODE,
            FLAGI_CODE_64
        );

    /*
     * 64-bit TSS zajmuje dwa klasyczne sloty GDT.
     */
    gdt->tss =
        zbuduj_deskryptor_tss(
            baza_tss,
            LIMIT_TSS_X86_64
        );
}

/* =========================================================================
 * 11. KONTROLA ZBUDOWANEJ TABELI
 * ========================================================================= */

bool gdt_ma_poprawne_atrybuty(
    const TablicaGDT& gdt
) {
    /*
     * Null musi byc rzeczywiscie zerowy.
     */
    const uint8_t* null_bytes =
        reinterpret_cast<const uint8_t*>(
            &gdt.null
        );

    for (size_t i = 0;
         i < sizeof(gdt.null);
         ++i) {

        if (null_bytes[i] != 0) {
            return false;
        }
    }

    if (gdt.kernel_code.dostep !=
            DOSTEP_KERNEL_CODE ||
        (gdt.kernel_code.limit_gorny_flagi &
         0xF0U) !=
            FLAGI_CODE_64) {

        return false;
    }

    if (gdt.kernel_data.dostep !=
            DOSTEP_KERNEL_DATA ||
        (gdt.kernel_data.limit_gorny_flagi &
         0xF0U) !=
            FLAGI_DATA) {

        return false;
    }

    if (gdt.user_data.dostep !=
            DOSTEP_USER_DATA ||
        (gdt.user_data.limit_gorny_flagi &
         0xF0U) !=
            FLAGI_DATA) {

        return false;
    }

    if (gdt.user_code.dostep !=
            DOSTEP_USER_CODE ||
        (gdt.user_code.limit_gorny_flagi &
         0xF0U) !=
            FLAGI_CODE_64) {

        return false;
    }

    if (gdt.tss.dostep !=
            DOSTEP_TSS_AVAILABLE_64 ||
        gdt.tss.zarezerwowane !=
            0) {

        return false;
    }

    return true;
}

/* =========================================================================
 * 12. ODCZYT REJESTROW SEGMENTOW / GDTR
 * ========================================================================= */

uint16_t odczytaj_cs() {
    uint16_t v = 0;

    asm volatile(
        "mov %%cs, %0"
        : "=r"(v)
    );

    return v;
}

uint16_t odczytaj_ds() {
    uint16_t v = 0;

    asm volatile(
        "mov %%ds, %0"
        : "=r"(v)
    );

    return v;
}

uint16_t odczytaj_es() {
    uint16_t v = 0;

    asm volatile(
        "mov %%es, %0"
        : "=r"(v)
    );

    return v;
}

uint16_t odczytaj_ss() {
    uint16_t v = 0;

    asm volatile(
        "mov %%ss, %0"
        : "=r"(v)
    );

    return v;
}

RejestrGDT odczytaj_gdtr() {
    RejestrGDT gdtr{};

    asm volatile(
        "sgdt %0"
        : "=m"(gdtr)
        :
        : "memory"
    );

    return gdtr;
}

/* =========================================================================
 * 13. WERYFIKACJA PO LGDT
 * ========================================================================= */

void sprawdz_zaladowana_gdt(
    const TablicaGDT* oczekiwana
) {
    if (!oczekiwana) {
        gdt_zatrzymaj_cpu();
    }

    const RejestrGDT aktywny =
        odczytaj_gdtr();

    const uint64_t oczekiwany_adres =
        reinterpret_cast<uint64_t>(
            oczekiwana
        );

    const uint16_t oczekiwany_limit =
        static_cast<uint16_t>(
            sizeof(TablicaGDT) -
            1U
        );

    if (aktywny.adres !=
            oczekiwany_adres ||
        aktywny.rozmiar !=
            oczekiwany_limit) {

        gdt_zatrzymaj_cpu();
    }

    /*
     * zaladuj_zaktualizowane_gdt() przeladowuje CS/DS/ES/SS.
     * FS i GS celowo nie sa sprawdzane ani zmieniane - ich bazy moga
     * pozniej sluzyc do TLS/per-CPU przez MSR.
     */
    if (odczytaj_cs() !=
            SELEKTOR_KODU_JADRA ||
        odczytaj_ds() !=
            SELEKTOR_DANYCH_JADRA ||
        odczytaj_es() !=
            SELEKTOR_DANYCH_JADRA ||
        odczytaj_ss() !=
            SELEKTOR_DANYCH_JADRA) {

        gdt_zatrzymaj_cpu();
    }
}

} // namespace

/* =========================================================================
 * 14. PUBLICZNA INICJALIZACJA
 * ========================================================================= */

extern "C" void InicjalizujGDT() {
    /*
     * Chronimy caly proces budowania i publikacji.
     *
     * Jest to istotne przy ponownej inicjalizacji: nawet maskowalne IRQ
     * nie powinno wykonac kodu polegajacego na starej konfiguracji w samym
     * srodku przejscia.
     *
     * NMI moze wystapic, dlatego budujemy NIEAKTYWNA kopie GDT.
     */
    const StanPrzerwan stan_irq =
        zapisz_i_wylacz_przerwania();

    uint8_t nowy_indeks =
        0;

    if (aktywna_tablica_gdt == 0U) {
        nowy_indeks =
            1U;
    } else {
        /*
         * Dla 0xFF (pierwszy start) i 1 wybieramy slot 0.
         */
        nowy_indeks =
            0U;
    }

    TablicaGDT* nowa =
        &tablice_gdt[
            nowy_indeks
        ];

    zbuduj_gdt(
        nowa
    );

    if (!gdt_ma_poprawne_atrybuty(
            *nowa)) {

        gdt_zatrzymaj_cpu();
    }

    const uint64_t adres_gdt =
        reinterpret_cast<uint64_t>(
            nowa
        );

    if (!adres_kanoniczny_48(
            adres_gdt) ||
        adres_gdt >
            UINT64_MAX -
            (sizeof(TablicaGDT) - 1U) ||
        !adres_kanoniczny_48(
            adres_gdt +
            sizeof(TablicaGDT) -
            1U)) {

        gdt_zatrzymaj_cpu();
    }

    RejestrGDT gdtr{};

    gdtr.rozmiar =
        static_cast<uint16_t>(
            sizeof(TablicaGDT) -
            1U
        );

    gdtr.adres =
        adres_gdt;

    /*
     * Assembly wykonuje LGDT i przeładowuje segmenty kernela.
     */
    zaladuj_zaktualizowane_gdt(
        reinterpret_cast<uint64_t>(
            &gdtr
        )
    );

    /*
     * Nie publikujemy indeksu dopoki sprzet nie potwierdzi nowej GDT.
     */
    sprawdz_zaladowana_gdt(
        nowa
    );

    aktywna_tablica_gdt =
        nowy_indeks;

    asm volatile(
        ""
        :
        :
        : "memory"
    );

    przywroc_przerwania(
        stan_irq
    );
}
