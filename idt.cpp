#ifdef BURSZTYN_IDT_HOST_TEST
extern unsigned long host_idt_irq_save();
extern void host_idt_irq_restore(unsigned long);
#endif
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

#include "idt.h"
#include "scheduler.h"
#include "pzb.h"
#include "stos_jadra.h"

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
constexpr uint16_t LICZBA_AKTYWNYCH_ISR = 256;

struct DynamicznyIrqWpis {
    IrqHandlerFn handler;
    void* context;
};

static DynamicznyIrqWpis dynamiczne_irq[LICZBA_WPISOW_IDT] = {};
static uint8_t wektory_zaalokowane[(LICZBA_WPISOW_IDT + 7) / 8] = {};

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
#ifdef BURSZTYN_IDT_HOST_TEST
    return host_idt_irq_save();
#else
    uint64_t rflags = 0;

    asm volatile(
        "pushfq\n"
        "popq %0\n"
        "cli"
        : "=r"(rflags)
        :
        : "memory");

    return rflags;
#endif
}

void przywroc_przerwania(uint64_t rflags) {
#ifdef BURSZTYN_IDT_HOST_TEST
    host_idt_irq_restore(rflags);
#else
    if ((rflags & (1ULL << 9)) != 0)
        asm volatile("sti" ::: "memory");
#endif
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

extern "C" uint64_t g_ostatni_bws;

extern "C" uint64_t tss_pobierz_rsp0();
extern "C" bool VmmDiagnostykaWpisu(uint64_t va, uint64_t* out_wpis);
extern "C" uint8_t stack_bottom[];
extern "C" uint8_t stack_top[];
extern "C" uint64_t tss_ist_podstawa(uint32_t indeks);
extern "C" uint64_t tss_ist_szczyt(uint32_t indeks);

void uint_do_dec_str(uint64_t wartosc,
                     char* bufor,
                     size_t pojemnosc) {
    if (!bufor || pojemnosc == 0) return;
    if (wartosc == 0) {
        if (pojemnosc > 1) { bufor[0] = '0'; bufor[1] = '\0'; }
        else { bufor[0] = '\0'; }
        return;
    }
    char tmp[24] = {};
    size_t n = 0;
    while (wartosc > 0 && n < sizeof(tmp)) {
        tmp[n++] = static_cast<char>('0' + (wartosc % 10ULL));
        wartosc /= 10ULL;
    }
    size_t out = 0;
    while (n > 0 && out + 1 < pojemnosc) {
        bufor[out++] = tmp[--n];
    }
    bufor[out] = '\0';
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

    /*
     * Sprzetowa rama przerwania w x86-64 Long Mode (AMD64 / Intel 64):
     * W przeciwienstwie do legacy 32-bit x86, architektura x86-64 ZAWSZE
     * wypycha pelna 5-qwordowa rame:
     *   [SS, RSP, RFLAGS, CS, RIP] (oraz opcjonalny kod bledu).
     * Dzieje sie tak NIEZALEZNIE od zmiany CPL (zarowno Ring 3 -> Ring 0,
     * jak i Ring 0 -> Ring 0) oraz niezaleznie od mechanizmu IST.
     *
     * Stuby w przerwania.S normalizuja wyjatki bez kodu bledu przez PUSH $0,
     * a nastepnie PUSH_GPRS (15 rejestrow). Dzieki temu struktura
     * RejestryStanowe ma dokladnie te same, niezmienne offsety dla wszystkich
     * wyjatkow/przerwan:
     *   stan->stary_rsp (offset 0xA0) to przerwany sprzetowy RSP,
     *   stan->stary_ss  (offset 0xA8) to przerwany sprzetowy SS.
     */
    uint64_t handler_rsp = 0;
    asm volatile("mov %%rsp, %0" : "=r"(handler_rsp));

    const bool ist_aktywne =
        (stan->wektor_przerwania == 8 ||
         stan->wektor_przerwania == 2 ||
         stan->wektor_przerwania == 18);
    const uint64_t przerwany_rsp = stan->stary_rsp;

    const uint64_t cr2_check = (stan->wektor_przerwania == 14 || stan->wektor_przerwania == 8) ? odczytaj_cr2() : 0;
    uint64_t sbase = 0, stop = 0;
    if ((stan->wektor_przerwania == 14 || stan->wektor_przerwania == 8) &&
        (stos_czy_adres_w_guard_page(cr2_check, aktualny_pid, &sbase, &stop) ||
         stos_czy_adres_w_guard_page(przerwany_rsp, aktualny_pid, &sbase, &stop))) {
        serial_wypisz("\n[STACK] KRYTYCZNE przekroczenie stosu jadra\n");
        wypisz_awaryjnie("\n[STACK] KRYTYCZNE przekroczenie stosu jadra\n");

        char dec_buf[32] = {};
        char hex_buf[17] = {};

        serial_wypisz("[STACK] pid=");
        if (aktualny_pid >= 0) {
            uint_do_dec_str(static_cast<uint64_t>(aktualny_pid), dec_buf, sizeof(dec_buf));
            serial_wypisz(dec_buf);
        } else {
            serial_wypisz("-1");
        }

        serial_wypisz("\n[STACK] program=");
        if (aktualny_pid >= 0 && aktualny_pid < static_cast<int>(MAKS_PROCESOW) &&
            tablica_procesow[aktualny_pid].sciezka_pliku[0] != '\0') {
            serial_wypisz(tablica_procesow[aktualny_pid].sciezka_pliku);
        } else {
            serial_wypisz("(brak/jadro)");
        }

        serial_wypisz("\n[STACK] rsp=0x");
        uint_do_hex_str(przerwany_rsp, hex_buf);
        serial_wypisz(hex_buf);

        serial_wypisz("\n[STACK] stackbase=0x");
        uint_do_hex_str(sbase, hex_buf);
        serial_wypisz(hex_buf);

        serial_wypisz("\n[STACK] stacktop=0x");
        uint_do_hex_str(stop, hex_buf);
        serial_wypisz(hex_buf);

        /*
         * Metryke used liczymy WYLACZNIE, gdy przerwany RSP daje sie
         * powiazac ze znanym slotem. Trzy przypadki:
         *  - RSP w [stackbase, stacktop]: zwykle zuzycie stop - rsp.
         *  - RSP w guard page slotu (ponizej stackbase): rzeczywiste
         *    przepelnienie w dol; used = stop - rsp pokazuje glebokosc
         *    wraz z wtargnieciem w guard (flaga rspwguard=1).
         *  - RSP poza slotem (stos startowy, IST, tresc stosu odczytana
         *    ze zlej ramki): brak miarodajnej metryki; used=UNKNOWN
         *    z flaga rspoutsidemanagedstack=1 (zakaz unsigned underflow
         *    i zatruwania znaku wodnego).
         */
        const bool rsp_w_zarzadzanym =
            (sbase != 0) && (stop > sbase) &&
            (przerwany_rsp >= sbase) && (przerwany_rsp <= stop);
        const bool rsp_w_guardzie =
            !rsp_w_zarzadzanym && (sbase != 0) && (stop > sbase) &&
            (przerwany_rsp >= sbase - ROZMIAR_GUARD_PAGE_BAJTY) &&
            (przerwany_rsp < sbase);
        if (rsp_w_zarzadzanym || rsp_w_guardzie) {
            uint64_t used = stop - przerwany_rsp;
            serial_wypisz("\n[STACK] used=");
            uint_do_dec_str(used, dec_buf, sizeof(dec_buf));
            serial_wypisz(dec_buf);
            if (rsp_w_guardzie) {
                serial_wypisz("\n[STACK] rspwguard=1");
            }
        } else {
            serial_wypisz("\n[STACK] used=UNKNOWN");
            serial_wypisz("\n[STACK] rspoutsidemanagedstack=1");
        }

        {
            int wlasciciel = -2;
            uint64_t baza = 0, szczyt = 0;
            const StosRodzaj rodzaj =
                stos_klasyfikuj_rsp(przerwany_rsp, &wlasciciel,
                                    &baza, &szczyt);
            serial_wypisz("\n[STACK] stos=");
            serial_wypisz(stos_nazwa_rodzaju(rodzaj));
            if (rodzaj == STOS_PROCES_JADRA) {
                serial_wypisz(" pid=");
                uint_do_dec_str(static_cast<uint64_t>(wlasciciel),
                                dec_buf, sizeof(dec_buf));
                serial_wypisz(dec_buf);
            }
            serial_wypisz(" baza=0x");
            uint_do_hex_str(baza, hex_buf);
            serial_wypisz(hex_buf);
            serial_wypisz(" szczyt=0x");
            uint_do_hex_str(szczyt, hex_buf);
            serial_wypisz(hex_buf);
        }

        serial_wypisz("\n[STACK] handler_rsp=0x");
        uint_do_hex_str(handler_rsp, hex_buf);
        serial_wypisz(hex_buf);

        serial_wypisz("\n[STACK] rama=0x");
        uint_do_hex_str(reinterpret_cast<uint64_t>(stan), hex_buf);
        serial_wypisz(hex_buf);

        serial_wypisz("\n[STACK] rip=0x");
        uint_do_hex_str(stan->adres_powrotu, hex_buf);
        serial_wypisz(hex_buf);

        serial_wypisz("\n[STACK] cs=0x");
        uint_do_hex_str(stan->rejestr_cs, hex_buf);
        serial_wypisz(hex_buf);

        serial_wypisz("\n[STACK] ss=0x");
        uint_do_hex_str(stan->stary_ss, hex_buf);
        serial_wypisz(hex_buf);

        serial_wypisz("\n[STACK] tss_rsp0=0x");
        uint_do_hex_str(tss_pobierz_rsp0(), hex_buf);
        serial_wypisz(hex_buf);

        serial_wypisz("\n[STACK] boot_baza=0x");
        uint_do_hex_str(reinterpret_cast<uint64_t>(stack_bottom), hex_buf);
        serial_wypisz(hex_buf);
        serial_wypisz(" boot_szczyt=0x");
        uint_do_hex_str(reinterpret_cast<uint64_t>(stack_top), hex_buf);
        serial_wypisz(hex_buf);

        if (ist_aktywne) {
            uint32_t ist_idx = (stan->wektor_przerwania == 8) ? 1 :
                               (stan->wektor_przerwania == 2) ? 2 : 3;
            serial_wypisz("\n[STACK] ist_aktywne=");
            uint_do_dec_str(ist_idx, dec_buf, sizeof(dec_buf));
            serial_wypisz(dec_buf);
            serial_wypisz(" ist_baza=0x");
            uint_do_hex_str(tss_ist_podstawa(ist_idx), hex_buf);
            serial_wypisz(hex_buf);
            serial_wypisz(" ist_szczyt=0x");
            uint_do_hex_str(tss_ist_szczyt(ist_idx), hex_buf);
            serial_wypisz(hex_buf);
        }

        if (stan->wektor_przerwania == 14 || stan->wektor_przerwania == 8) {
            uint64_t wpis = 0;
            const bool obecna = VmmDiagnostykaWpisu(cr2_check, &wpis);
            serial_wypisz("\n[STACK] cr2=0x");
            uint_do_hex_str(cr2_check, hex_buf);
            serial_wypisz(hex_buf);
            serial_wypisz("\n[STACK] present=");
            serial_wypisz(obecna ? "1" : "0");
            serial_wypisz(" pfec_p=");
            {
                char bit[2] = {};
                // Dla #DF kod bledu jest zawsze 0; bit P nie dotyczy
                // bledu podwojnego, lecz pierwotnego #PF (CR2 aktualne).
                bit[0] = (stan->wektor_przerwania == 14 &&
                          (stan->kod_bledu & 1ULL)) ? '1' : '0';
                serial_wypisz(bit);
            }
        }

        serial_wypisz("\n[STACK] lastbws=");
        uint_do_dec_str(g_ostatni_bws, dec_buf, sizeof(dec_buf));
        serial_wypisz(dec_buf);
        serial_wypisz("\n");

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
        const uint64_t cr2 = odczytaj_cr2();
        wypisz_hex_awaryjnie(
            "  CR2:          ",
            cr2);

        char buf[32] = {};
        serial_wypisz("[PF] pid=");
        if (aktualny_pid >= 0) {
            uint_do_dec_str(static_cast<uint64_t>(aktualny_pid), buf, sizeof(buf));
            serial_wypisz(buf);
        } else {
            serial_wypisz("-1");
        }
        serial_wypisz("\n[PF] program=");
        if (aktualny_pid >= 0 && aktualny_pid < static_cast<int>(MAKS_PROCESOW) &&
            tablica_procesow[aktualny_pid].sciezka_pliku[0] != '\0') {
            serial_wypisz(tablica_procesow[aktualny_pid].sciezka_pliku);
        } else {
            serial_wypisz("(brak/jadro)");
        }
        serial_wypisz("\n[PF] lastbws=");
        uint_do_dec_str(g_ostatni_bws, buf, sizeof(buf));
        serial_wypisz(buf);

        serial_wypisz("\n[PF] rip=0x");
        char hex_buf[17] = {};
        uint_do_hex_str(stan->adres_powrotu, hex_buf);
        serial_wypisz(hex_buf);

        serial_wypisz("\n[PF] cr2=0x");
        uint_do_hex_str(cr2, hex_buf);
        serial_wypisz(hex_buf);

        serial_wypisz("\n[PF] rsp=0x");
        uint_do_hex_str(przerwany_rsp, hex_buf);
        serial_wypisz(hex_buf);
        serial_wypisz("\n");

        wypisz_hex_awaryjnie("  RSP:          ", przerwany_rsp);
        wypisz_awaryjnie("  Stos [top 8]:\n");
        /*
         * Zrzucamy wylacznie znany, zmapowany stos. Luzna kontrola
         * < 0xFFFF8000... przepuszczala tresci pamieci (np. 0x688F38),
         * co dawalo mylace zrzuty spoza przerwanego kontekstu.
         */
        const uint64_t* stos_ptr = reinterpret_cast<const uint64_t*>(przerwany_rsp);
        int wlasciciel_dump = -2;
        uint64_t baza_dump = 0, szczyt_dump = 0;
        const StosRodzaj rodzaj_dump =
            stos_klasyfikuj_rsp(przerwany_rsp, &wlasciciel_dump,
                                &baza_dump, &szczyt_dump);
            if (rodzaj_dump != STOS_NIEZNANY &&
                przerwany_rsp >= baza_dump &&
                przerwany_rsp + 64 <= szczyt_dump) {
            for (int i = 0; i < 8; ++i) {
                char prefix[20] = "    [+0x..]:    ";
                prefix[8] = "0123456789ABCDEF"[(i * 8) >> 4];
                prefix[9] = "0123456789ABCDEF"[(i * 8) & 0xF];
                wypisz_hex_awaryjnie(prefix, stos_ptr[i]);
                serial_wypisz("[PF] stos[");
                buf[0] = static_cast<char>('0' + i); buf[1] = '\0';
                serial_wypisz(buf);
                serial_wypisz("]=0x");
                uint_do_hex_str(stos_ptr[i], hex_buf);
                serial_wypisz(hex_buf);
                serial_wypisz("\n");
            }
        } else {
            wypisz_awaryjnie("  Stos:         niedostepny (RSP poza znanym stosem)\n");
            serial_wypisz("[PF] stos=niedostepny (RSP poza znanym stosem)\n");
        }
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
     * Konfiguracja dedykowanych stosow IST w TSS:
     *   #DF (wektor 8)  -> IST1 (awaryjny stos z wlasnym guard page)
     *   NMI (wektor 2)  -> IST2
     *   #MC (wektor 18) -> IST3
     */
    if (wektor == 8) {
        wpis.interrupt_stack = 1;
    } else if (wektor == 2) {
        wpis.interrupt_stack = 2;
    } else if (wektor == 18) {
        wpis.interrupt_stack = 3;
    } else {
        wpis.interrupt_stack = 0;
    }

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

#include "virtio_input_activation_diag.h"
extern "C" uint64_t WspolnaObslugaPrzerwan(
    uint64_t rsp) {

    if (rsp == 0)
        zatrzymaj_system();

    RejestryStanowe* stan =
        reinterpret_cast<RejestryStanowe*>(rsp);

    const uint64_t wektor64 =
        stan->wektor_przerwania;

#if BURSZTYNDIAGVIRTIOINPUTINIT
    input_activation_entry(wektor64, stan->adres_powrotu, stan->kod_bledu,
        wektor64 == 14 || wektor64 == 8 ? odczytaj_cr2() : 0);
#endif

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

    const bool przerwano_ring0 = ((stan->rejestr_cs & 3ULL) == 0ULL);
    if (przerwano_ring0 && aktualny_pid > 0 && aktualny_pid < MAKS_PROCESOW) {
        const char* irq_ctx = "IRQOTHER";
        if (wektor == WEKTOR_TIMERA) irq_ctx = "IRQTIMER";
        else if (wektor == WEKTOR_MYSZY) irq_ctx = "IRQXHCI";
        stos_probkuj_rsp_kontekst(aktualny_pid, stan->stary_rsp, irq_ctx);
        uint64_t handler_rsp = 0;
        asm volatile("mov %%rsp, %0" : "=r"(handler_rsp));
        stos_probkuj_rsp_kontekst(aktualny_pid, handler_rsp, irq_ctx);
    }

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
            if (wektor >= IDT_PIERWSZY_WEKTOR_DYNAMICZNY && wektor <= IDT_OSTATNI_WEKTOR_DYNAMICZNY) {
                DynamicznyIrqWpis wpis = dynamiczne_irq[wektor];
                if (wpis.handler) {
                    wpis.handler(wpis.context);
                }
            }
            break;
    }

    /*
     * EOI musi zostac wyslane przed ewentualna zmiana CR3/RSP.
     * Funkcja sprawdza ISR, wiec nie wykonuje podwojnego EOI.
     */
    wyslij_eoi_jesli_potrzebne(
        wektor);

    if (wektor != WEKTOR_TIMERA) {
#if BURSZTYNDIAGVIRTIOINPUTINIT
        input_activation_exit(wektor);
#endif
        return rsp;
    }

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

    extern volatile bool ekran_zajety;
    const bool pid_idle =
        aktualny_pid == 0 && !__atomic_load_n(&ekran_zajety, __ATOMIC_ACQUIRE);

    bool proces_juz_pusty = false;
    bool proces_blokuje_w_bws38 = false;

    if (aktualny_pid > 0 &&
        aktualny_pid < MAKS_PROCESOW) {
        const int stan_procesu=tablica_procesow[aktualny_pid].stan;
        proces_juz_pusty=stan_procesu==PROCES_PUSTY||stan_procesu==PROCES_KONCZACY;
        proces_blokuje_w_bws38 =
            stan_procesu == PROCES_ZABLOKOWANY_ZDARZENIE;
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
        proces_juz_pusty ||
        proces_blokuje_w_bws38) {
#if BURSZTYNDIAGVIRTIOINPUTINIT
        if (input_activation_trace.armed) input_activation_log("scheduler enter", wektor);
#endif
        const uint64_t next_rsp = PrzelaczKontekst(rsp);
#if BURSZTYNDIAGVIRTIOINPUTINIT
        input_activation_exit(wektor);
#endif
        return next_rsp;
    }

    /*
     * Jezeli timer przerwal zwykly kod Ring 0, pozostajemy w tym samym
     * kontekscie. Wyjatek BWS38 powyzej jest bezpieczny: scheduler zapisuje
     * kompletna rame IRQ na prywatnym stosie procesu, a po wybudzeniu IRETQ
     * wznawia ten sam SYSCALL za HLT.
     */
#if BURSZTYNDIAGVIRTIOINPUTINIT
    input_activation_exit(wektor);
#endif
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

/* =========================================================================
 * DYNAMICZNE ZARZADZANIE WEKTORAMI IRQ (MSI / MSI-X)
 * ========================================================================= */

static_assert(IDT_PIERWSZY_WEKTOR_DYNAMICZNY > 47, "Dynamic MSI vectors must exclude PS/2 IRQ12 and fixed vectors");
extern "C" int idt_alokuj_wektor_irq(void) {
    const uint64_t rflags = zapisz_i_wylacz_przerwania();
    int zaalokowany = -1;
    for (int v = IDT_PIERWSZY_WEKTOR_DYNAMICZNY; v <= IDT_OSTATNI_WEKTOR_DYNAMICZNY; ++v) {
        const int bajt = v / 8;
        const int bit = v % 8;
        if ((wektory_zaalokowane[bajt] & (1U << bit)) == 0) {
#if defined(BURSZTYNDIAGXHCIPOINTING) && BURSZTYNDIAGXHCIPOINTING
            if(dynamiczne_irq[v].handler || v==44)__builtin_trap();
#endif
            wektory_zaalokowane[bajt] |= static_cast<uint8_t>(1U << bit);
            dynamiczne_irq[v].handler = nullptr;
            dynamiczne_irq[v].context = nullptr;
            zaalokowany = v;
            break;
        }
    }
    przywroc_przerwania(rflags);
    return zaalokowany;
}

extern "C" bool idt_zwolnij_wektor_irq(uint8_t vector) {
    if (vector < IDT_PIERWSZY_WEKTOR_DYNAMICZNY || vector > IDT_OSTATNI_WEKTOR_DYNAMICZNY) {
        return false;
    }
    const uint64_t rflags = zapisz_i_wylacz_przerwania();
    const int bajt = vector / 8;
    const int bit = vector % 8;
    if ((wektory_zaalokowane[bajt] & (1U << bit)) == 0) {
        przywroc_przerwania(rflags);
        return false;
    }
    wektory_zaalokowane[bajt] &= static_cast<uint8_t>(~(1U << bit));
    dynamiczne_irq[vector].handler = nullptr;
    dynamiczne_irq[vector].context = nullptr;
    przywroc_przerwania(rflags);
    return true;
}

extern "C" bool idt_zarejestruj_irq(uint8_t vector, IrqHandlerFn handler, void* context) {
    if (vector < IDT_PIERWSZY_WEKTOR_DYNAMICZNY || vector > IDT_OSTATNI_WEKTOR_DYNAMICZNY || !handler) {
        return false;
    }
    const uint64_t rflags = zapisz_i_wylacz_przerwania();
    const int bajt = vector / 8;
    const int bit = vector % 8;
    if ((wektory_zaalokowane[bajt] & (1U << bit)) == 0) {
        przywroc_przerwania(rflags);
        return false;
    }
    if (dynamiczne_irq[vector].handler) {
        przywroc_przerwania(rflags);
        return false; // Preserve the existing vector's handler/context ownership.
    }
    dynamiczne_irq[vector].handler = handler;
    dynamiczne_irq[vector].context = context;
    przywroc_przerwania(rflags);
    return true;
}

extern "C" bool idt_wyrejestruj_irq(uint8_t vector) {
    if (vector < IDT_PIERWSZY_WEKTOR_DYNAMICZNY || vector > IDT_OSTATNI_WEKTOR_DYNAMICZNY) {
        return false;
    }
    const uint64_t rflags = zapisz_i_wylacz_przerwania();
    dynamiczne_irq[vector].handler = nullptr;
    dynamiczne_irq[vector].context = nullptr;
    przywroc_przerwania(rflags);
    return true;
}

extern "C" uint32_t idt_zaalokowane_wektory_liczba(void) {
    const uint64_t rflags = zapisz_i_wylacz_przerwania();
    uint32_t count = 0;
    for (int v = IDT_PIERWSZY_WEKTOR_DYNAMICZNY; v <= IDT_OSTATNI_WEKTOR_DYNAMICZNY; ++v) {
        const int bajt = v / 8;
        const int bit = v % 8;
        if ((wektory_zaalokowane[bajt] & (1U << bit)) != 0) {
            ++count;
        }
    }
    przywroc_przerwania(rflags);
    return count;
}
