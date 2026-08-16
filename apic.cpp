/*
 * Bursztyn OS - APIC / IOAPIC
 *
 * Mechanizm:
 *  - maskowanie starego kontrolera PIC 8259A,
 *  - aktywacja Local APIC w trybie xAPIC,
 *  - konfiguracja okresowego timera Local APIC,
 *  - konfiguracja IOAPIC dla klawiatury PS/2 i myszy PS/2.
 *
 * Aktualne zalozenie platformy:
 *  - jeden IOAPIC pod fizycznym adresem 0xFEC00000,
 *  - IRQ == GSI dla IRQ 1 i IRQ 12,
 *  - tryb xAPIC MMIO (nie x2APIC).
 *
 * Docelowo adres IOAPIC, GSI base i Interrupt Source Overrides
 * powinny byc pobierane z ACPI MADT.
 */

#include <stdint.h>
#include "pamiec.h"

extern "C" {
    extern volatile uint32_t* baza_lapic_wirtualna;
    extern volatile uint32_t* baza_ioapic_wirtualna;
}

/* =========================================================================
 * STALE SPRZETOWE
 * ========================================================================= */

namespace {

constexpr uint32_t IA32_APIC_BASE_MSR = 0x1B;

constexpr uint64_t IA32_APIC_BASE_BSP       = 1ULL << 8;
constexpr uint64_t IA32_APIC_BASE_X2APIC    = 1ULL << 10;
constexpr uint64_t IA32_APIC_BASE_ENABLE    = 1ULL << 11;
constexpr uint64_t IA32_APIC_BASE_ADDR_MASK = 0x0000000FFFFFF000ULL;

constexpr uint64_t DOMYSLNY_IOAPIC_FIZYCZNY = 0xFEC00000ULL;

constexpr uint16_t PIC1_DATA = 0x21;
constexpr uint16_t PIC2_DATA = 0xA1;

constexpr uint32_t LAPIC_ID                = 0x020;
constexpr uint32_t LAPIC_EOI               = 0x0B0;
constexpr uint32_t LAPIC_SPURIOUS           = 0x0F0;
constexpr uint32_t LAPIC_LVT_TIMER          = 0x320;
constexpr uint32_t LAPIC_TIMER_INITIAL      = 0x380;
constexpr uint32_t LAPIC_TIMER_DIVIDE       = 0x3E0;

constexpr uint32_t LAPIC_SVR_ENABLE         = 1U << 8;
constexpr uint32_t LAPIC_LVT_MASKED         = 1U << 16;
constexpr uint32_t LAPIC_TIMER_PERIODIC     = 1U << 17;

constexpr uint8_t WEKTOR_TIMERA             = 32;
constexpr uint8_t WEKTOR_KLAWIATURY         = 33;
constexpr uint8_t WEKTOR_MYSZY              = 44;

/*
 * IDT Bursztyna instaluje obecnie bramy 0..47.
 * Dlatego wektor spurious musi znajdowac sie w tym zakresie.
 */
constexpr uint8_t WEKTOR_SPURIOUS            = 47;

constexpr uint8_t IRQ_KLAWIATURY             = 1;
constexpr uint8_t IRQ_MYSZY                  = 12;

constexpr uint32_t IOAPIC_REG_ID             = 0x00;
constexpr uint32_t IOAPIC_REG_VERSION        = 0x01;
constexpr uint32_t IOAPIC_REDIRECTION_BASE   = 0x10;

constexpr uint32_t IOAPIC_MASKED             = 1U << 16;

/*
 * APIC Timer divide configuration:
 * 0b0011 = dzielnik 16.
 */
constexpr uint32_t APIC_TIMER_DIVIDE_BY_16   = 0x3;

/*
 * Wartosci timera nie da sie poprawnie przeliczyc na Hz bez kalibracji
 * wzgledem PIT/HPET/TSC. Zachowujemy dotychczasowa wartosc Bursztyna.
 */
constexpr uint32_t APIC_TIMER_INITIAL_COUNT  = 0x05FFFFFFU;

/*
 * Flagi VMM zgodne z obecnym Bursztynem:
 * bit 0 = Present
 * bit 1 = Writable
 * bit 4 = Cache Disable (PCD)
 */
constexpr uint64_t FLAGI_MMIO = 0b11ULL | 0x10ULL;

/* =========================================================================
 * NISKIE I/O / CPU
 * ========================================================================= */

static inline void wyjscie_port_bajt(uint16_t port,
                                     uint8_t wartosc) {
    asm volatile(
        "outb %0, %1"
        :
        : "a"(wartosc), "Nd"(port)
        : "memory");
}

static inline uint64_t odczytaj_msr(uint32_t msr) {
    uint32_t dolny = 0;
    uint32_t gorny = 0;

    asm volatile(
        "rdmsr"
        : "=a"(dolny), "=d"(gorny)
        : "c"(msr));

    return
        (static_cast<uint64_t>(gorny) << 32) |
        static_cast<uint64_t>(dolny);
}

static inline void zapisz_msr(uint32_t msr,
                              uint64_t wartosc) {
    const uint32_t dolny =
        static_cast<uint32_t>(
            wartosc & 0xFFFFFFFFULL);

    const uint32_t gorny =
        static_cast<uint32_t>(
            wartosc >> 32);

    asm volatile(
        "wrmsr"
        :
        : "a"(dolny), "d"(gorny), "c"(msr)
        : "memory");
}

static inline void cpuid(uint32_t leaf,
                         uint32_t subleaf,
                         uint32_t* eax,
                         uint32_t* ebx,
                         uint32_t* ecx,
                         uint32_t* edx) {
    uint32_t a = 0;
    uint32_t b = 0;
    uint32_t c = 0;
    uint32_t d = 0;

    asm volatile(
        "cpuid"
        : "=a"(a), "=b"(b), "=c"(c), "=d"(d)
        : "a"(leaf), "c"(subleaf)
        : "memory");

    if (eax) *eax = a;
    if (ebx) *ebx = b;
    if (ecx) *ecx = c;
    if (edx) *edx = d;
}

static bool procesor_ma_apic() {
    uint32_t edx = 0;

    cpuid(
        1,
        0,
        nullptr,
        nullptr,
        nullptr,
        &edx);

    return (edx & (1U << 9)) != 0;
}

static uint64_t zapisz_i_wylacz_przerwania() {
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

static void przywroc_przerwania(uint64_t rflags) {
    if ((rflags & (1ULL << 9)) != 0)
        asm volatile("sti" ::: "memory");
}

[[noreturn]]
static void zatrzymaj_system_apic() {
    while (true)
        asm volatile("cli; hlt");
}

static inline void bariera_mmio() {
    asm volatile("" ::: "memory");
}

static void mapuj_strone_mmio(uint64_t adres_fizyczny) {
    const uint64_t strona =
        adres_fizyczny & ~0xFFFULL;

    ZmapujStrone(
        reinterpret_cast<void*>(strona),
        reinterpret_cast<void*>(strona),
        FLAGI_MMIO);

    /*
     * Usuwamy potencjalny stary wpis TLB dla tego adresu.
     */
    asm volatile(
        "invlpg (%0)"
        :
        : "r"(strona)
        : "memory");
}

/* =========================================================================
 * LOCAL APIC
 * ========================================================================= */

static inline uint32_t lapic_odczytaj(uint32_t offset) {
    if (!baza_lapic_wirtualna)
        return 0;

    const uint32_t wartosc =
        baza_lapic_wirtualna[offset / 4];

    bariera_mmio();
    return wartosc;
}

static inline void lapic_zapisz(uint32_t offset,
                                uint32_t wartosc) {
    if (!baza_lapic_wirtualna)
        return;

    baza_lapic_wirtualna[offset / 4] =
        wartosc;

    bariera_mmio();

    /*
     * Odczyt wymusza zakonczenie posted write na typowych
     * implementacjach MMIO APIC.
     */
    (void)baza_lapic_wirtualna[LAPIC_ID / 4];
}

static uint8_t pobierz_lapic_id() {
    return static_cast<uint8_t>(
        (lapic_odczytaj(LAPIC_ID) >> 24) &
        0xFFU);
}

/* =========================================================================
 * IOAPIC
 * ========================================================================= */

static inline uint32_t ioapic_odczytaj(uint32_t rejestr) {
    if (!baza_ioapic_wirtualna)
        return 0;

    baza_ioapic_wirtualna[0] = rejestr;
    bariera_mmio();

    const uint32_t wynik =
        baza_ioapic_wirtualna[4];

    bariera_mmio();
    return wynik;
}

static inline void ioapic_zapisz(uint32_t rejestr,
                                 uint32_t wartosc) {
    if (!baza_ioapic_wirtualna)
        return;

    baza_ioapic_wirtualna[0] = rejestr;
    bariera_mmio();

    baza_ioapic_wirtualna[4] = wartosc;
    bariera_mmio();

    /*
     * Read-back ogranicza ryzyko kontynuowania konfiguracji
     * przed zakonczeniem zapisu MMIO.
     */
    (void)ioapic_odczytaj(rejestr);
}

static uint32_t ioapic_maksymalny_wpis() {
    const uint32_t wersja =
        ioapic_odczytaj(IOAPIC_REG_VERSION);

    return (wersja >> 16) & 0xFFU;
}

static void zamaskuj_wpis_ioapic(uint32_t irq) {
    const uint32_t rejestr_dolny =
        IOAPIC_REDIRECTION_BASE +
        irq * 2U;

    uint32_t dolny =
        ioapic_odczytaj(rejestr_dolny);

    dolny |= IOAPIC_MASKED;

    ioapic_zapisz(
        rejestr_dolny,
        dolny);
}

static void zamaskuj_wszystkie_wejscia_ioapic() {
    const uint32_t maks =
        ioapic_maksymalny_wpis();

    for (uint32_t irq = 0;
         irq <= maks;
         ++irq) {
        zamaskuj_wpis_ioapic(irq);
    }
}

static bool przekieruj_przerwanie_ioapic_impl(
    uint32_t irq,
    uint8_t wektor_docelowy,
    uint8_t apic_id) {

    const uint32_t maks =
        ioapic_maksymalny_wpis();

    if (irq > maks)
        return false;

    /*
     * Dolne 32 bity:
     *  0..7   - wektor
     *  8..10  - Fixed delivery
     *  11     - physical destination
     *  13     - active high
     *  15     - edge triggered
     *  16     - unmasked
     */
    const uint32_t dolny_docelowy =
        static_cast<uint32_t>(
            wektor_docelowy);

    const uint32_t dolny_maskowany =
        dolny_docelowy |
        IOAPIC_MASKED;

    /*
     * W trybie physical destination ID Local APIC
     * znajduje sie w bitach 56..63 wpisu, czyli 24..31
     * gornej polowy.
     */
    const uint32_t gorny =
        static_cast<uint32_t>(apic_id) << 24;

    const uint32_t rejestr_dolny =
        IOAPIC_REDIRECTION_BASE +
        irq * 2U;

    const uint32_t rejestr_gorny =
        rejestr_dolny + 1U;

    /*
     * Bezpieczna kolejnosc:
     *  1. wpis pozostaje zamaskowany,
     *  2. ustawiamy destination APIC ID,
     *  3. ustawiamy finalna dolna polowe i odmaskowujemy.
     */
    ioapic_zapisz(
        rejestr_dolny,
        dolny_maskowany);

    ioapic_zapisz(
        rejestr_gorny,
        gorny);

    ioapic_zapisz(
        rejestr_dolny,
        dolny_docelowy);

    return true;
}

static bool skonfiguruj_timer_lapic() {
    if (!baza_lapic_wirtualna)
        return false;

    /*
     * Najpierw zatrzymujemy i maskujemy timer, aby konfiguracja
     * nie wygenerowala przerwania w polowie ustawiania rejestrow.
     */
    lapic_zapisz(
        LAPIC_TIMER_INITIAL,
        0);

    lapic_zapisz(
        LAPIC_LVT_TIMER,
        static_cast<uint32_t>(WEKTOR_TIMERA) |
        LAPIC_TIMER_PERIODIC |
        LAPIC_LVT_MASKED);

    lapic_zapisz(
        LAPIC_TIMER_DIVIDE,
        APIC_TIMER_DIVIDE_BY_16);

    lapic_zapisz(
        LAPIC_TIMER_INITIAL,
        APIC_TIMER_INITIAL_COUNT);

    lapic_zapisz(
        LAPIC_LVT_TIMER,
        static_cast<uint32_t>(WEKTOR_TIMERA) |
        LAPIC_TIMER_PERIODIC);

    return true;
}

} // namespace

/* =========================================================================
 * GLOBALNE BAZY MMIO
 *
 * idt.cpp korzysta bezposrednio z baza_lapic_wirtualna do wysylania EOI.
 * ========================================================================= */

extern "C" {

volatile uint32_t* baza_lapic_wirtualna = nullptr;
volatile uint32_t* baza_ioapic_wirtualna = nullptr;

}

/* =========================================================================
 * PUBLICZNE FUNKCJE
 * ========================================================================= */

void wylacz_przestarzale_pic() {
    /*
     * Nie remapujemy PIC, bo nie bedzie juz zrodlem normalnych IRQ.
     * Maskujemy wszystkie wejscia obu ukladow 8259A.
     */
    wyjscie_port_bajt(
        PIC1_DATA,
        0xFF);

    wyjscie_port_bajt(
        PIC2_DATA,
        0xFF);
}

void przekieruj_przerwanie_ioapic(
    uint8_t irq,
    uint8_t wektor_docelowy) {

    if (!baza_ioapic_wirtualna ||
        !baza_lapic_wirtualna) {
        return;
    }

    const uint8_t apic_id =
        pobierz_lapic_id();

    (void)przekieruj_przerwanie_ioapic_impl(
        irq,
        wektor_docelowy,
        apic_id);
}

extern "C" void inicjalizuj_apic() {
    const uint64_t stare_rflags =
        zapisz_i_wylacz_przerwania();

    /*
     * Bursztyn korzysta z Local APIC do timera planisty.
     * Brak APIC jest obecnie bledem krytycznym platformy.
     */
    if (!procesor_ma_apic())
        zatrzymaj_system_apic();

    wylacz_przestarzale_pic();

    uint64_t apic_base =
        odczytaj_msr(
            IA32_APIC_BASE_MSR);

    /*
     * Ta implementacja obsluguje xAPIC MMIO.
     * Jesli firmware uruchomil CPU w x2APIC, nie wolno traktowac
     * adresu bazowego jak zwyklego MMIO.
     */
    if ((apic_base &
         IA32_APIC_BASE_X2APIC) != 0) {
        zatrzymaj_system_apic();
    }

    const uint64_t lapic_fizyczny =
        apic_base &
        IA32_APIC_BASE_ADDR_MASK;

    if (lapic_fizyczny == 0)
        zatrzymaj_system_apic();

    /*
     * Zapewniamy mapowanie MMIO zanim wykonamy pierwszy dostep
     * do rejestrow Local APIC.
     */
    mapuj_strone_mmio(
        lapic_fizyczny);

    baza_lapic_wirtualna =
        reinterpret_cast<volatile uint32_t*>(
            lapic_fizyczny);

    /*
     * Zachowujemy wszystkie pozostale bity MSR i aktywujemy
     * globalny bit APIC enable.
     */
    apic_base |=
        IA32_APIC_BASE_ENABLE;

    zapisz_msr(
        IA32_APIC_BASE_MSR,
        apic_base);

    /*
     * Software Enable Local APIC.
     * Zachowujemy pozostale bity rejestru SVR.
     */
    uint32_t svr =
        lapic_odczytaj(
            LAPIC_SPURIOUS);

    svr &=
        ~0xFFU;

    svr |=
        static_cast<uint32_t>(
            WEKTOR_SPURIOUS);

    svr |=
        LAPIC_SVR_ENABLE;

    lapic_zapisz(
        LAPIC_SPURIOUS,
        svr);

    /*
     * Wyczyszczenie ewentualnego starego stanu EOI nie jest wymagane.
     * Rejestr EOI zapisujemy dopiero po realnych przerwaniach w idt.cpp.
     */
    (void)LAPIC_EOI;
    (void)IA32_APIC_BASE_BSP;

    /*
     * IOAPIC - aktualnie standardowy adres PC/QEMU.
     * MADT zastapi to zalozenie w dalszym rozwoju systemu.
     */
    mapuj_strone_mmio(
        DOMYSLNY_IOAPIC_FIZYCZNY);

    baza_ioapic_wirtualna =
        reinterpret_cast<volatile uint32_t*>(
            DOMYSLNY_IOAPIC_FIZYCZNY);

    /*
     * Odczyt ID i VERSION dodatkowo wymusza realny dostep MMIO.
     */
    const uint32_t ioapic_id =
        ioapic_odczytaj(
            IOAPIC_REG_ID);

    const uint32_t ioapic_version =
        ioapic_odczytaj(
            IOAPIC_REG_VERSION);

    (void)ioapic_id;

    /*
     * Version == 0xFFFFFFFF zwykle wskazuje brak urzadzenia/MMIO.
     * Zerowy rejestr wersji takze nie jest prawidlowa konfiguracja
     * typowego IOAPIC.
     */
    if (ioapic_version == 0 ||
        ioapic_version == 0xFFFFFFFFU) {
        zatrzymaj_system_apic();
    }

    const uint32_t maks_wpis =
        (ioapic_version >> 16) &
        0xFFU;

    if (maks_wpis < IRQ_MYSZY)
        zatrzymaj_system_apic();

    /*
     * Zanim zaczniemy routing, maskujemy wszystkie linie,
     * aby pozostalosci konfiguracji firmware nie generowaly
     * nieoczekiwanych przerwan.
     */
    zamaskuj_wszystkie_wejscia_ioapic();

    const uint8_t apic_id =
        pobierz_lapic_id();

    if (!przekieruj_przerwanie_ioapic_impl(
            IRQ_KLAWIATURY,
            WEKTOR_KLAWIATURY,
            apic_id)) {
        zatrzymaj_system_apic();
    }

    if (!przekieruj_przerwanie_ioapic_impl(
            IRQ_MYSZY,
            WEKTOR_MYSZY,
            apic_id)) {
        zatrzymaj_system_apic();
    }

    if (!skonfiguruj_timer_lapic())
        zatrzymaj_system_apic();

    /*
     * Przywracamy IF tylko wtedy, gdy przed wejsciem do funkcji
     * przerwania byly wlaczone. W obecnym kernel_main() APIC jest
     * inicjalizowany przed globalnym STI, wiec pozostana wylaczone.
     */
    przywroc_przerwania(
        stare_rflags);
}
