/*
 * Bursztyn OS - Interrupt Descriptor Table (IDT)
 *
 * Obsluguje:
 *  - wyjatki procesora 0..31,
 *  - timer Local APIC      -> wektor 32,
 *  - klawiature PS/2       -> wektor 33,
 *  - mysz PS/2             -> wektor 44,
 *  - spurious Local APIC   -> wektor 47.
 *
 * Stub ISR w asemblerze musi normalizowac rame przerwania do struktury
 * RejestryStanowe z pzb.h: dla wyjatkow bez sprzetowego kodu bledu powinien
 * dostarczyc sztuczne 0, tak aby uklad stosu byl identyczny dla wszystkich
 * wektorow.
 */

#include <stdint.h>
#include <stddef.h>

#include "scheduler.h"
#include "pzb.h"

/* =========================================================================
 * POLACZENIA Z INNYMI MODULAMI
 * ========================================================================= */

extern "C" {

void wypisz_na_ekranie(const char* tekst);

void obsluga_przerwania_klawiatury();
void obsluga_przerwania_myszy();
void obsluga_przerwania_zegara();

void zaladuj_zaktualizowane_idt(uint64_t adres_idtr);

extern uint64_t tablica_isr[];

/*
 * W poprawionym apic.cpp baza LAPIC ma C-linkage.
 */
extern volatile uint32_t* baza_lapic_wirtualna;

}

/* =========================================================================
 * STALE
 * ========================================================================= */

namespace {

constexpr uint16_t SELEKTOR_KODU_JADRA = 0x08;

/*
 * P=1, DPL=0, typ=0xE (64-bit interrupt gate).
 */
constexpr uint8_t FLAGA_INTERRUPT_GATE_RING0 = 0x8E;

constexpr uint16_t LICZBA_WPISOW_IDT = 256;
constexpr uint16_t LICZBA_AKTYWNYCH_ISR = 48;

constexpr uint8_t WEKTOR_TIMERA = 32;
constexpr uint8_t WEKTOR_KLAWIATURY = 33;
constexpr uint8_t WEKTOR_MYSZY = 44;
constexpr uint8_t WEKTOR_SPURIOUS = 47;

constexpr uint32_t LAPIC_ISR_BASE = 0x100;
constexpr uint32_t LAPIC_EOI = 0x0B0;

constexpr uint16_t PORT_COM1 = 0x3F8;

volatile bool panic_aktywna = false;

/* =========================================================================
 * STRUKTURY IDT
 * ========================================================================= */

struct DeskryptorIDT {
    uint16_t offset_czesc1;
    uint16_t selektor_kodu;
    uint8_t interrupt_stack;
    uint8_t typ_oraz_atrybuty;
    uint16_t offset_czesc2;
    uint32_t offset_czesc3;
    uint32_t puste_zero;
} __attribute__((packed));

struct RejestrIDT {
    uint16_t rozmiar;
    uint64_t adres;
} __attribute__((packed));

static_assert(sizeof(DeskryptorIDT) == 16,
              "Deskryptor IDT x86_64 musi miec 16 bajtow");

static_assert(sizeof(RejestrIDT) == 10,
              "IDTR w x86_64 musi miec 10 bajtow");

/* =========================================================================
 * AWARYJNY SERIAL
 * ========================================================================= */

static inline void serial_outb(uint16_t port,
                               uint8_t wartosc) {
    asm volatile(
        "outb %0, %1"
        :
        : "a"(wartosc), "Nd"(port)
        : "memory");
}

void serial_wypisz(const char* tekst) {
    if (!tekst) return;

    for (size_t i = 0; tekst[i] != '\0'; ++i)
        serial_outb(
            PORT_COM1,
            static_cast<uint8_t>(tekst[i]));
}

/* =========================================================================
 * POMOCNICZE OPERACJE CPU
 * ========================================================================= */

uint64_t zapisz_i_wylacz_przerwania() {
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

void przywroc_przerwania(uint64_t rflags) {
    if ((rflags & (1ULL << 9)) != 0)
        asm volatile("sti" ::: "memory");
}

uint64_t odczytaj_cr2() {
    uint64_t cr2 = 0;

    asm volatile(
        "mov %%cr2, %0"
        : "=r"(cr2));

    return cr2;
}

[[noreturn]]
void zatrzymaj_system() {
    while (true)
        asm volatile("cli; hlt");
}

/* =========================================================================
 * OPERACJE TEKSTOWE BEZ LIBC
 * ========================================================================= */

void uint_do_hex_str(uint64_t wartosc,
                     char* bufor) {
    if (!bufor) return;

    static constexpr char CYFRY[] =
        "0123456789ABCDEF";

    for (int i = 15; i >= 0; --i) {
        bufor[i] =
            CYFRY[wartosc & 0xFULL];

        wartosc >>= 4;
    }

    bufor[16] = '\0';
}

void zbuduj_linie_hex(const char* prefiks,
                      uint64_t wartosc,
                      char* bufor,
                      size_t pojemnosc) {
    if (!bufor || pojemnosc == 0)
        return;

    size_t poz = 0;

    if (prefiks) {
        while (prefiks[poz] != '\0' &&
               poz + 1 < pojemnosc) {
            bufor[poz] = prefiks[poz];
            ++poz;
        }
    }

    if (poz + 2 < pojemnosc) {
        bufor[poz++] = '0';
        bufor[poz++] = 'x';
    }

    char hex[17] = {};
    uint_do_hex_str(wartosc, hex);

    for (size_t i = 0;
         i < 16 && poz + 1 < pojemnosc;
         ++i) {
        bufor[poz++] = hex[i];
    }

    if (poz + 1 < pojemnosc)
        bufor[poz++] = '\n';

    bufor[poz < pojemnosc ? poz : pojemnosc - 1] =
        '\0';
}

void wypisz_awaryjnie(const char* tekst) {
    /*
     * COM1 jest pierwsza linia diagnostyczna. Jesli awaria powstala
     * w podsystemie grafiki, serial nadal ma szanse zadzialac.
     */
    serial_wypisz(tekst);

    /*
     * Ekran jest tylko druga linia diagnostyczna. Gdyby samo rysowanie
     * wywolalo kolejny wyjatek, panic_aktywna zatrzyma rekurencje.
     */
    wypisz_na_ekranie(tekst);
}

void wypisz_hex_awaryjnie(const char* prefiks,
                          uint64_t wartosc) {
    char linia[96] = {};

    zbuduj_linie_hex(
        prefiks,
        wartosc,
        linia,
        sizeof(linia));

    wypisz_awaryjnie(linia);
}

/* =========================================================================
 * NAZWY WYJATKOW
 * ========================================================================= */

const char* nazwa_wyjatku(uint64_t wektor) {
    static const char* const NAZWY[32] = {
        "#DE Divide Error",                 // 0
        "#DB Debug",                        // 1
        "NMI",                              // 2
        "#BP Breakpoint",                   // 3
        "#OF Overflow",                     // 4
        "#BR BOUND Range Exceeded",         // 5
        "#UD Invalid Opcode",               // 6
        "#NM Device Not Available",         // 7
        "#DF Double Fault",                 // 8
        "Coprocessor Segment Overrun",      // 9 (legacy)
        "#TS Invalid TSS",                  // 10
        "#NP Segment Not Present",          // 11
        "#SS Stack-Segment Fault",          // 12
        "#GP General Protection",           // 13
        "#PF Page Fault",                   // 14
        "Reserved",                         // 15
        "#MF x87 Floating-Point",           // 16
        "#AC Alignment Check",              // 17
        "#MC Machine Check",                // 18
        "#XM SIMD Floating-Point",          // 19
        "#VE Virtualization",               // 20
        "#CP Control Protection",           // 21
        "Reserved",                         // 22
        "Reserved",                         // 23
        "Reserved",                         // 24
        "Reserved",                         // 25
        "Reserved",                         // 26
        "Reserved",                         // 27
        "#HV Hypervisor Injection",         // 28
        "#VC VMM Communication",            // 29
        "#SX Security",                     // 30
        "Reserved"                          // 31
    };

    if (wektor < 32)
        return NAZWY[wektor];

    return "Nieznany wyjatek";
}

/* =========================================================================
 * LOCAL APIC - BEZPIECZNE EOI
 * ========================================================================= */

bool lapic_wektor_w_obsludze(uint8_t wektor) {
    if (!baza_lapic_wirtualna)
        return false;

    /*
     * Rejestry ISR Local APIC:
     * 0x100, 0x110, ... 0x170.
     * Kazdy obsluguje 32 kolejne wektory.
     */
    const uint32_t grupa =
        static_cast<uint32_t>(wektor) / 32U;

    const uint32_t bit =
        static_cast<uint32_t>(wektor) % 32U;

    if (grupa >= 8)
        return false;

    const uint32_t offset =
        LAPIC_ISR_BASE +
        grupa * 0x10U;

    const uint32_t wartosc =
        baza_lapic_wirtualna[offset / 4U];

    asm volatile("" ::: "memory");

    return
        (wartosc & (1U << bit)) != 0;
}

void wyslij_eoi_jesli_potrzebne(uint8_t wektor) {
    if (!baza_lapic_wirtualna)
        return;

    /*
     * To rozwiazuje dwa problemy naraz:
     *  - spurious interrupt nie powinien dostac EOI,
     *  - starsze sterowniki Bursztyna (np. klawiatura) moga jeszcze
     *    wysylac EOI samodzielnie. Nie wysylamy wtedy drugiego EOI.
     */
    if (!lapic_wektor_w_obsludze(wektor))
        return;

    baza_lapic_wirtualna[
        LAPIC_EOI / 4U] = 0;

    asm volatile("" ::: "memory");
}

/* =========================================================================
 * PANIC WYJATKU CPU
 * ========================================================================= */

[[noreturn]]
void panic_wyjatek(const RejestryStanowe* stan) {
    asm volatile("cli" ::: "memory");

    if (panic_aktywna) {
        serial_wypisz(
            "\n[PANIC] Zagniezdzony wyjatek podczas obslugi BSOD.\n");

        zatrzymaj_system();
    }

    panic_aktywna = true;

    if (!stan) {
        serial_wypisz(
            "\n[PANIC] Brak poprawnej ramy przerwania.\n");

        zatrzymaj_system();
    }

    wypisz_awaryjnie(
        "\n========================================================\n");

    wypisz_awaryjnie(
        "  KRYTYCZNY BLAD SYSTEMU (BSOD) - WYJATEK PROCESORA\n");

    wypisz_awaryjnie(
        "========================================================\n");

    wypisz_awaryjnie("  Typ: ");
    wypisz_awaryjnie(
        nazwa_wyjatku(
            stan->wektor_przerwania));
    wypisz_awaryjnie("\n");

    wypisz_hex_awaryjnie(
        "  Wektor:       ",
        stan->wektor_przerwania);

    wypisz_hex_awaryjnie(
        "  Kod bledu:    ",
        stan->kod_bledu);

    wypisz_hex_awaryjnie(
        "  RIP:          ",
        stan->adres_powrotu);

    wypisz_hex_awaryjnie(
        "  CS:           ",
        stan->rejestr_cs);

    wypisz_hex_awaryjnie(
        "  RFLAGS:       ",
        stan->rflags);

    if (stan->wektor_przerwania == 14) {
        wypisz_hex_awaryjnie(
            "  CR2:          ",
            odczytaj_cr2());
    }

    /*
     * Informacja, czy wyjatek powstal w Ring 0 czy Ring 3.
     */
    if ((stan->rejestr_cs & 3ULL) == 3ULL) {
        wypisz_awaryjnie(
            "  Zrodlo:       Ring 3\n");
    } else {
        wypisz_awaryjnie(
            "  Zrodlo:       Ring 0\n");
    }

    wypisz_awaryjnie(
        "\nSystem zostal zatrzymany ze wzgledow bezpieczenstwa.\n");

    zatrzymaj_system();
}

/* =========================================================================
 * IDT
 * ========================================================================= */

} // namespace

DeskryptorIDT tablica_idt[LICZBA_WPISOW_IDT]
    __attribute__((aligned(4096)));

RejestrIDT wskaznik_idtr
    __attribute__((aligned(16)));

static_assert(sizeof(tablica_idt) == 4096,
              "Pelna IDT x86_64 powinna zajmowac 4096 bajtow");

void UstawWpisIDT(uint8_t wektor,
                  uint64_t procedura_isr,
                  uint8_t flagi) {
    DeskryptorIDT& wpis =
        tablica_idt[wektor];

    wpis.offset_czesc1 =
        static_cast<uint16_t>(
            procedura_isr & 0xFFFFULL);

    wpis.selektor_kodu =
        SELEKTOR_KODU_JADRA;

    /*
     * IST = 0. Obecny TSS Bursztyna nie konfiguruje jeszcze
     * osobnych stosow IST dla #DF/#NMI/#MC.
     */
    wpis.interrupt_stack = 0;

    wpis.typ_oraz_atrybuty =
        flagi;

    wpis.offset_czesc2 =
        static_cast<uint16_t>(
            (procedura_isr >> 16) &
            0xFFFFULL);

    wpis.offset_czesc3 =
        static_cast<uint32_t>(
            (procedura_isr >> 32) &
            0xFFFFFFFFULL);

    wpis.puste_zero = 0;
}

/* =========================================================================
 * WSPOLNY DYSPozytor PRZERWAN
 * ========================================================================= */

extern "C" uint64_t WspolnaObslugaPrzerwan(
    uint64_t rsp) {

    if (rsp == 0)
        zatrzymaj_system();

    RejestryStanowe* stan =
        reinterpret_cast<RejestryStanowe*>(rsp);

    const uint64_t wektor64 =
        stan->wektor_przerwania;

    if (wektor64 < 32) {
        panic_wyjatek(stan);
    }

    /*
     * Stub-y tej wersji IDT sa dostepne tylko dla 0..47.
     * Wektor spoza tego zakresu oznacza uszkodzona rame albo rozjazd
     * miedzy tablica_isr a konfiguracja kontrolera przerwan.
     */
    if (wektor64 >= LICZBA_AKTYWNYCH_ISR) {
        wypisz_awaryjnie(
            "\n[PANIC] Nieprawidlowy wektor w ramie przerwania.\n");

        wypisz_hex_awaryjnie(
            "  Wektor: ",
            wektor64);

        zatrzymaj_system();
    }

    const uint8_t wektor =
        static_cast<uint8_t>(wektor64);

    switch (wektor) {
        case WEKTOR_TIMERA:
            obsluga_przerwania_zegara();
            break;

        case WEKTOR_KLAWIATURY:
            obsluga_przerwania_klawiatury();
            break;

        case WEKTOR_MYSZY:
            obsluga_przerwania_myszy();
            break;

        case WEKTOR_SPURIOUS:
            /*
             * Spurious vector nie wymaga handlera ani EOI.
             * Funkcja EOI ponizej dodatkowo sprawdzi ISR.
             */
            break;

        default:
            /*
             * Pozostale wektory 34..46 sa obecnie zarezerwowane.
             * Jezeli Local APIC raportuje je jako in-service,
             * nalezy je zakonczyc EOI, aby nie zablokowac APIC.
             */
            break;
    }

    /*
     * EOI musi zostac wyslane przed ewentualna zmiana CR3/RSP.
     * Funkcja sprawdza ISR, wiec nie wykonuje podwojnego EOI.
     */
    wyslij_eoi_jesli_potrzebne(
        wektor);

    if (wektor != WEKTOR_TIMERA)
        return rsp;

    /*
     * Scheduler:
     * - przelaczamy zadania po timerze, gdy przerwanie nastapilo w Ring 3,
     * - PID 0 moze uruchomic pierwsze gotowe zadanie,
     * - jesli obecny proces zostal oznaczony jako pusty, opuszczamy go.
     *
     * Nie indeksujemy tablica_procesow poza MAKS_PROCESOW.
     */
    const bool przerwano_ring3 =
        (stan->rejestr_cs & 3ULL) == 3ULL;

    const bool pid_idle =
        aktualny_pid == 0;

    bool proces_juz_pusty = false;

    if (aktualny_pid > 0 &&
        aktualny_pid < MAKS_PROCESOW) {
        proces_juz_pusty =
            tablica_procesow[aktualny_pid].stan ==
            PROCES_PUSTY;
    }

    /*
     * Uszkodzony PID jest bledem jadra. Nie wolno uzyc go jako indeksu.
     */
    if (aktualny_pid >= MAKS_PROCESOW) {
        wypisz_awaryjnie(
            "\n[PANIC] Scheduler ma nieprawidlowy aktualny_pid.\n");

        wypisz_hex_awaryjnie(
            "  PID: ",
            static_cast<uint64_t>(aktualny_pid));

        zatrzymaj_system();
    }

    if (przerwano_ring3 ||
        pid_idle ||
        proces_juz_pusty) {
        return PrzelaczKontekst(rsp);
    }

    /*
     * Jezeli timer przerwal kod Ring 0, pozostajemy w tym samym
     * kontekscie. Chroni to m.in. rame SYSCALL/SWAPGS przed
     * przelaczeniem procesu w niekompatybilnym formacie stosu.
     */
    return rsp;
}

/* =========================================================================
 * INICJALIZACJA IDT
 * ========================================================================= */

extern "C" void InicjalizujIDT() {
    const uint64_t stare_rflags =
        zapisz_i_wylacz_przerwania();

    /*
     * Najpierw zerujemy cala tablice. Nieaktywne wektory 48..255
     * pozostaja jawnie nieobecne zamiast zawierac przypadkowe dane.
     */
    for (uint16_t i = 0;
         i < LICZBA_WPISOW_IDT;
         ++i) {

        tablica_idt[i].offset_czesc1 = 0;
        tablica_idt[i].selektor_kodu = 0;
        tablica_idt[i].interrupt_stack = 0;
        tablica_idt[i].typ_oraz_atrybuty = 0;
        tablica_idt[i].offset_czesc2 = 0;
        tablica_idt[i].offset_czesc3 = 0;
        tablica_idt[i].puste_zero = 0;
    }

    /*
     * Obecna tablica ISR udostepnia wektory 0..47.
     * To jest zgodne z poprawionym APIC:
     * 32 timer, 33 klawiatura, 44 mysz, 47 spurious.
     */
    for (uint16_t i = 0;
         i < LICZBA_AKTYWNYCH_ISR;
         ++i) {

        const uint64_t adres_isr =
            tablica_isr[i];

        if (adres_isr == 0) {
            serial_wypisz(
                "[IDT] BLAD: tablica_isr zawiera pusty adres.\n");

            zatrzymaj_system();
        }

        UstawWpisIDT(
            static_cast<uint8_t>(i),
            adres_isr,
            FLAGA_INTERRUPT_GATE_RING0);
    }

    wskaznik_idtr.rozmiar =
        static_cast<uint16_t>(
            sizeof(tablica_idt) - 1);

    wskaznik_idtr.adres =
        reinterpret_cast<uint64_t>(
            &tablica_idt[0]);

    zaladuj_zaktualizowane_idt(
        reinterpret_cast<uint64_t>(
            &wskaznik_idtr));

    /*
     * Nie wlaczamy przerwan na sile. Przywracamy stan IF sprzed
     * inicjalizacji IDT.
     */
    przywroc_przerwania(
        stare_rflags);
}
