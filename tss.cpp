/*
 * Bursztyn OS - Task State Segment (TSS) dla x86_64 Long Mode
 *
 * TSS jest wymagany przez procesor mimo plaskiego modelu segmentacji.
 *
 * W Bursztyn OS odpowiada za:
 *
 *   - RSP0:
 *       prywatny stos Ring 0 aktualnego procesu, uzywany przy przejsciu
 *       Ring 3 -> Ring 0 przez brame przerwania/wyjatku,
 *
 *   - IST1:
 *       awaryjny stos dla #DF (Double Fault, wektor 8),
 *
 *   - IST2:
 *       osobny stos dla NMI (wektor 2),
 *
 *   - IST3:
 *       osobny stos dla #MC (Machine Check, wektor 18),
 *
 *   - IOPB:
 *       brak bitmapy portow I/O dla Ring 3.
 *
 * WAZNE:
 * Samo wpisanie adresow IST do TSS nie aktywuje IST. Odpowiedni deskryptor
 * IDT musi ustawic pole IST:
 *
 *   #DF -> 1
 *   NMI -> 2
 *   #MC -> 3
 *
 * Obecny stary idt.cpp wpisuje 0 dla wszystkich wektorow, wiec nalezy
 * zaktualizowac IDT osobno.
 *
 * TSS x86_64 jest struktura SPRZETOWA. Jej layout nie moze sie zmienic.
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* =========================================================================
 * 1. STALE
 * ========================================================================= */

namespace {

constexpr uint16_t SELEKTOR_TSS_BURSZTYN =
    0x28U;

/*
 * 32 KiB na krytyczny stos.
 *
 * Stosy sa statyczne, poniewaz TSS jest inicjalizowany przed gotowym heapem.
 * Rozmiar jest wielokrotnoscia strony i daje znacznie wiecej zapasu niz
 * minimalna rama wyjatku.
 *
 * Docelowo, po dojrzalszym VMM:
 *   [guard page][IST stack][guard page]
 * bedzie jeszcze bezpieczniejsze.
 */
constexpr size_t ROZMIAR_STOSU_IST =
    32U * 1024U;

constexpr size_t WYROWNANIE_STOSU_IST =
    4096U;

static_assert(
    (ROZMIAR_STOSU_IST % 4096U) == 0,
    "Stos IST powinien miec rozmiar wielokrotnosci strony"
);

/* =========================================================================
 * 2. FUNKCJE POMOCNICZE CPU
 * ========================================================================= */

[[noreturn]]
void tss_zatrzymaj_cpu() {
    /*
     * TSS jest inicjalizowany zanim IDT musi byc gotowe.
     * Nie uzywamy tutaj UD2, aby blad inicjalizacji nie zamienil sie
     * niepotrzebnie w triple fault przed zaladowaniem IDT.
     */
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

bool adres_kanoniczny(
    uint64_t adres
) {
    /*
     * Dla 48-bitowego canonical addressing:
     *
     * bit 47 == 0 -> bity 63..48 musza byc 0
     * bit 47 == 1 -> bity 63..48 musza byc 1
     *
     * Aktualny Bursztyn korzysta z klasycznego 4-level paging,
     * wiec nie zakladamy jeszcze LA57.
     */
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

bool poprawny_szczyt_stosu(
    uint64_t rsp
) {
    if (rsp == 0) {
        return false;
    }

    if (!adres_kanoniczny(
            rsp)) {

        return false;
    }

    /*
     * Wszystkie stosy Ring 0 w obecnym kernelu sa 16-bajtowo wyrownane.
     * Jest to rowniez wymagane przez ABI System V AMD64 przed budowaniem
     * ram wywolan C/C++.
     */
    if ((rsp & 0xFULL) != 0) {
        return false;
    }

    return true;
}

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

} // namespace

/* =========================================================================
 * 3. SPRZETOWY LAYOUT TSS x86_64
 * ========================================================================= */

/*
 * Intel/AMD 64-bit TSS:
 *
 *   0x00  reserved                    4 B
 *   0x04  RSP0                       8 B
 *   0x0C  RSP1                       8 B
 *   0x14  RSP2                       8 B
 *   0x1C  reserved                    8 B
 *   0x24  IST1                       8 B
 *   0x2C  IST2                       8 B
 *   0x34  IST3                       8 B
 *   0x3C  IST4                       8 B
 *   0x44  IST5                       8 B
 *   0x4C  IST6                       8 B
 *   0x54  IST7                       8 B
 *   0x5C  reserved                    8 B
 *   0x64  reserved                    2 B
 *   0x66  I/O Map Base Address       2 B
 *
 * Razem: 0x68 = 104 bajty.
 */
struct tss_wpis {
    uint32_t zarezerwowane1;

    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;

    uint64_t zarezerwowane2;

    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;

    uint64_t zarezerwowane3;

    uint16_t zarezerwowane4;
    uint16_t iopb_offset;
} __attribute__((packed));

/* =========================================================================
 * 4. KONTROLA ABI SPRZETOWEGO
 * ========================================================================= */

static_assert(
    sizeof(tss_wpis) == 104,
    "x86_64 TSS musi miec dokladnie 104 bajty"
);

static_assert(
    alignof(tss_wpis) == 1,
    "tss_wpis jest packed zgodnie ze sprzetowym ABI"
);

static_assert(
    offsetof(tss_wpis, zarezerwowane1) == 0x00,
    "Nieprawidlowy offset TSS reserved0"
);

static_assert(
    offsetof(tss_wpis, rsp0) == 0x04,
    "Nieprawidlowy offset TSS.RSP0"
);

static_assert(
    offsetof(tss_wpis, rsp1) == 0x0C,
    "Nieprawidlowy offset TSS.RSP1"
);

static_assert(
    offsetof(tss_wpis, rsp2) == 0x14,
    "Nieprawidlowy offset TSS.RSP2"
);

static_assert(
    offsetof(tss_wpis, zarezerwowane2) == 0x1C,
    "Nieprawidlowy offset TSS reserved1"
);

static_assert(
    offsetof(tss_wpis, ist1) == 0x24,
    "Nieprawidlowy offset TSS.IST1"
);

static_assert(
    offsetof(tss_wpis, ist2) == 0x2C,
    "Nieprawidlowy offset TSS.IST2"
);

static_assert(
    offsetof(tss_wpis, ist3) == 0x34,
    "Nieprawidlowy offset TSS.IST3"
);

static_assert(
    offsetof(tss_wpis, ist4) == 0x3C,
    "Nieprawidlowy offset TSS.IST4"
);

static_assert(
    offsetof(tss_wpis, ist5) == 0x44,
    "Nieprawidlowy offset TSS.IST5"
);

static_assert(
    offsetof(tss_wpis, ist6) == 0x4C,
    "Nieprawidlowy offset TSS.IST6"
);

static_assert(
    offsetof(tss_wpis, ist7) == 0x54,
    "Nieprawidlowy offset TSS.IST7"
);

static_assert(
    offsetof(tss_wpis, zarezerwowane3) == 0x5C,
    "Nieprawidlowy offset TSS reserved2"
);

static_assert(
    offsetof(tss_wpis, zarezerwowane4) == 0x64,
    "Nieprawidlowy offset TSS reserved3"
);

static_assert(
    offsetof(tss_wpis, iopb_offset) == 0x66,
    "Nieprawidlowy offset TSS IOPB"
);

/* =========================================================================
 * 5. GLOBALNY TSS
 * ========================================================================= */

/*
 * Nazwa i C++ linkage pozostaja zgodne z obecnym gdt.cpp:
 *
 *   struct tss_wpis;
 *   extern struct tss_wpis globalny_tss;
 *
 * alignas(64) nie zmienia sprzetowego layoutu - jedynie polozenie obiektu
 * w pamieci.
 */
alignas(64)
tss_wpis globalny_tss = {};

/* =========================================================================
 * 6. AWARYJNE STOSY IST
 * ========================================================================= */

/*
 * Stos rosnie w dol. Do TSS wpisujemy adres jeden bajt ZA tablica.
 *
 * Uzywamy osobnego stosu dla kazdej krytycznej klasy wyjatku. Dzielenie
 * jednego IST pomiedzy #DF, NMI i #MC mogloby zniszczyc rame poprzedniego
 * krytycznego zdarzenia.
 */
namespace {

alignas(WYROWNANIE_STOSU_IST)
uint8_t stos_ist_double_fault[
    ROZMIAR_STOSU_IST
] = {};

alignas(WYROWNANIE_STOSU_IST)
uint8_t stos_ist_nmi[
    ROZMIAR_STOSU_IST
] = {};

alignas(WYROWNANIE_STOSU_IST)
uint8_t stos_ist_machine_check[
    ROZMIAR_STOSU_IST
] = {};

uint64_t szczyt_stosu_ist(
    uint8_t* stos
) {
    if (!stos) {
        return 0;
    }

    const uint64_t szczyt =
        reinterpret_cast<uint64_t>(
            stos +
            ROZMIAR_STOSU_IST
        );

    return szczyt;
}

void wyzeruj_tss() {
    uint8_t* bajty =
        reinterpret_cast<uint8_t*>(
            &globalny_tss
        );

    for (size_t i = 0;
         i < sizeof(globalny_tss);
         ++i) {

        bajty[i] = 0;
    }
}

} // namespace

/* =========================================================================
 * 7. INICJALIZACJA TSS
 * ========================================================================= */

extern "C" void inicjalizuj_tss(
    void* stos_jadra
) {
    /*
     * Funkcja jest wywolywana podczas bootu przed STI.
     *
     * Jesli zostanie kiedys wywolana ponownie, zachowujemy poprzedni IF,
     * aby nie tworzyc okna z czesciowo przebudowanym TSS.
     */
    const StanPrzerwan stan_irq =
        zapisz_i_wylacz_przerwania();

    const uint64_t rsp0 =
        reinterpret_cast<uint64_t>(
            stos_jadra
        );

    if (!poprawny_szczyt_stosu(
            rsp0)) {

        tss_zatrzymaj_cpu();
    }

    const uint64_t ist_df =
        szczyt_stosu_ist(
            stos_ist_double_fault
        );

    const uint64_t ist_nmi =
        szczyt_stosu_ist(
            stos_ist_nmi
        );

    const uint64_t ist_mc =
        szczyt_stosu_ist(
            stos_ist_machine_check
        );

    if (!poprawny_szczyt_stosu(
            ist_df) ||
        !poprawny_szczyt_stosu(
            ist_nmi) ||
        !poprawny_szczyt_stosu(
            ist_mc)) {

        tss_zatrzymaj_cpu();
    }

    /*
     * Reserved fields wymagane przez architekture pozostaja zero.
     */
    wyzeruj_tss();

    globalny_tss.rsp0 =
        rsp0;

    /*
     * Ring 1/Ring 2 nie sa obecnie uzywane.
     */
    globalny_tss.rsp1 = 0;
    globalny_tss.rsp2 = 0;

    /*
     * Mapa:
     *
     *   IST1 -> #DF
     *   IST2 -> NMI
     *   IST3 -> #MC
     */
    globalny_tss.ist1 =
        ist_df;

    globalny_tss.ist2 =
        ist_nmi;

    globalny_tss.ist3 =
        ist_mc;

    globalny_tss.ist4 = 0;
    globalny_tss.ist5 = 0;
    globalny_tss.ist6 = 0;
    globalny_tss.ist7 = 0;

    /*
     * Deskryptor GDT ma limit:
     *
     *   sizeof(TSS) - 1 == 103
     *
     * Ustawienie IOPB base na 104 powoduje:
     *
     *   iopb_offset > limit
     *
     * czyli mapa portow I/O nie istnieje.
     *
     * Ring 3 nie dostaje w ten sposob zadnego bezposredniego prawa IN/OUT.
     */
    globalny_tss.iopb_offset =
        static_cast<uint16_t>(
            sizeof(tss_wpis)
        );

    /*
     * Bariera kompilatora przed ewentualnym przywroceniem IF.
     */
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

/* =========================================================================
 * 8. AKTUALIZACJA RSP0 PRZEZ SCHEDULER
 * ========================================================================= */

extern "C" void ustaw_stos_jadra_tss(
    uint64_t rsp0
) {
    if (!poprawny_szczyt_stosu(
            rsp0)) {

        tss_zatrzymaj_cpu();
    }

    /*
     * RSP0 znajduje sie pod offsetem 4, wiec pole nie jest naturalnie
     * wyrownane do 8 bajtow. Nie polegamy na atomowosci potencjalnie
     * niewyrownanego 64-bitowego store.
     *
     * Na obecnym jednordzeniowym Bursztyn OS wyłączenie maskowalnych IRQ
     * gwarantuje, ze procesor nie wykona przejscia Ring 3 -> Ring 0 przez
     * zwykla brame przerwania w polowie zapisu.
     *
     * NMI/#MC/#DF powinny korzystac z IST i wtedy nie zależą od RSP0.
     */
    const StanPrzerwan stan_irq =
        zapisz_i_wylacz_przerwania();

    globalny_tss.rsp0 =
        rsp0;

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

/* =========================================================================
 * 9. LADOWANIE TASK REGISTER
 * ========================================================================= */

extern "C" void zaladuj_tss(
    uint16_t selektor_tss
) {
    /*
     * Aktualna GDT Bursztyn OS umieszcza 16-bajtowy deskryptor TSS pod
     * indeksem 5 -> selector 0x28.
     */
    if (selektor_tss !=
        SELEKTOR_TSS_BURSZTYN) {

        tss_zatrzymaj_cpu();
    }

    /*
     * TSS musi pochodzic z GDT (TI=0) i miec RPL0.
     */
    if ((selektor_tss &
         0x0007U) != 0) {

        tss_zatrzymaj_cpu();
    }

    uint16_t aktualny_tr = 0;

    asm volatile(
        "str %0"
        : "=rm"(aktualny_tr)
        :
        : "memory"
    );

    /*
     * Ponowne LTR na tym samym deskryptorze jest zbedne, a deskryptor po
     * pierwszym LTR jest oznaczany przez CPU jako "busy TSS".
     */
    if ((aktualny_tr &
         0xFFF8U) ==
        selektor_tss) {

        return;
    }

    /*
     * LTR sprawdzi sprzetowo typ, Present i limit deskryptora GDT.
     * Nie probujemy obchodzic #GP w przypadku blednego GDT.
     */
    asm volatile(
        "ltr %0"
        :
        : "rm"(selektor_tss)
        : "memory"
    );

    uint16_t sprawdzenie = 0;

    asm volatile(
        "str %0"
        : "=rm"(sprawdzenie)
        :
        : "memory"
    );

    if ((sprawdzenie &
         0xFFF8U) !=
        selektor_tss) {

        tss_zatrzymaj_cpu();
    }
}
