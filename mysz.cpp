/*
 * Bursztyn OS - sterownik myszy PS/2
 *
 * Obsluguje PS/2 ID 0 (3 bajty) oraz ID 3/4 (4 bajty z rolka).
 * Standardowy naglowek i osie:
 *
 *   bajt 0:
 *     bit 0 - lewy przycisk
 *     bit 1 - prawy przycisk
 *     bit 2 - srodkowy przycisk
 *     bit 3 - zawsze 1 w poprawnym pierwszym bajcie pakietu
 *     bit 4 - znak X
 *     bit 5 - znak Y
 *     bit 6 - overflow X
 *     bit 7 - overflow Y
 *
 *   bajt 1 - przesuniecie X
 *   bajt 2 - przesuniecie Y
 *
 * EOI Local APIC NIE jest wysylane w tym pliku.
 * Odpowiada za nie wspolny dispatcher IDT.
 */

#include <stdint.h>
#include <stdbool.h>
#include "mysz_input.h"
#include "input_device.h"
#include "ps2_mouse_parser.h"
#include "pointing_diag.h"
extern "C" void ps2_klawiatura_przetworz_scancode(uint8_t);
#ifdef BURSZTYN_PS2_HOST_TEST
extern uint8_t ps2_test_read(uint16_t);
extern void ps2_test_write(uint16_t,uint8_t);
#endif

/* =========================================================================
 * POLACZENIE Z PODSYSTEMEM GRAFIKI
 * ========================================================================= */

/* =========================================================================
 * STALE KONTROLERA PS/2
 * ========================================================================= */

namespace {

constexpr uint16_t PORT_PS2_DANE   = 0x60;
constexpr uint16_t PORT_PS2_STATUS = 0x64;
constexpr uint16_t PORT_PS2_CMD    = 0x64;

constexpr uint8_t STATUS_OUTPUT_FULL = 1U << 0;
constexpr uint8_t STATUS_INPUT_FULL  = 1U << 1;
constexpr uint8_t STATUS_AUX_DATA    = 1U << 5;

constexpr uint8_t KONTROLER_WLACZ_PORT_MYSZY = 0xA8;
constexpr uint8_t KONTROLER_CZYTAJ_KONFIG    = 0x20;
constexpr uint8_t KONTROLER_ZAPISZ_KONFIG    = 0x60;
constexpr uint8_t KONTROLER_DO_MYSZY          = 0xD4;

constexpr uint8_t KONFIG_IRQ_MYSZY            = 1U << 1;
constexpr uint8_t KONFIG_ZABLOKUJ_ZEGAR_MYSZY = 1U << 5;

constexpr uint8_t MYSZ_USTAW_DOMYSLNE         = 0xF6;
constexpr uint8_t MYSZ_WLACZ_RAPORTOWANIE     = 0xF4;

constexpr uint8_t MYSZ_ACK                     = 0xFA;
constexpr uint8_t MYSZ_RESEND                  = 0xFE;

constexpr uint32_t LIMIT_OCZEKIWANIA           = 100000U;
constexpr uint32_t LIMIT_PROB_RESEND            = 3U;



/* =========================================================================
 * STAN STEROWNIKA
 * ========================================================================= */

Ps2MouseParser parser{};
struct PendingReport { Ps2MouseReport report; uint64_t sequence; };
PendingReport pending[64]{};
uint64_t produced_sequence{},consumed_sequence{},overflow_sequence{};
uint8_t overflow_buttons{};
static bool newer_sequence(uint64_t a,uint64_t b) {
    return a!=b && uint64_t(a-b)<(UINT64_C(1)<<63);
}
static void dispatch_mouse_report(InputDevice* dev,const Ps2MouseReport& r) {
    InputEvent evs[10];size_t n=0;
    evs[n++]={INPUT_EV_REL,INPUT_REL_X,uint32_t(r.dx)};
    evs[n++]={INPUT_EV_REL,INPUT_REL_Y,uint32_t(r.dy)};
    evs[n++]={INPUT_EV_REL,INPUT_REL_WHEEL,uint32_t(r.wheel)};
    for(unsigned i=0;i<5;++i)evs[n++]={INPUT_EV_KEY,uint16_t(INPUT_BTN_LEFT+i),uint32_t((r.buttons>>i)&1)};
    evs[n++]={INPUT_EV_SYN,INPUT_SYN_REPORT,0};
    input_dispatch_events(dev,evs,n);
}
uint32_t pending_head{}, pending_tail{};
#if BURSZTYNDIAGPS2
struct PacketDiag {int64_t v[10];};
PacketDiag packet_diags[128]{};unsigned diag_written{},diag_read{};
#endif
void poll_mouse(InputDevice* dev) {
#if BURSZTYNDIAGPS2
    const unsigned available=__atomic_load_n(&diag_written,__ATOMIC_ACQUIRE);
    while(diag_read<available)pointing_diag("[PS2] byte/state/bytes[0..3]/dx/dy/overflow/rejected",packet_diags[diag_read++].v,10);
#endif
    for(unsigned budget=0;budget<64;++budget) {
        const uint32_t tail=__atomic_load_n(&pending_tail,__ATOMIC_RELAXED);
        if(tail==__atomic_load_n(&pending_head,__ATOMIC_ACQUIRE))break;
        const PendingReport entry=pending[tail];
        __atomic_store_n(&pending_tail,(tail+1)%64,__ATOMIC_RELEASE);
        if(!newer_sequence(entry.sequence,consumed_sequence))continue;
        consumed_sequence=entry.sequence;
        dispatch_mouse_report(dev,entry.report);
    }
    // Queue overload may lose motion, but must never leave a button stuck.
    // Only the producer writes this snapshot. SC publication plus a bounded
    // double read prevents mixing a sequence with a different button state.
    const uint64_t sequence=__atomic_load_n(&overflow_sequence,__ATOMIC_SEQ_CST);
    const uint8_t buttons=__atomic_load_n(&overflow_buttons,__ATOMIC_SEQ_CST);
    if(sequence && sequence==__atomic_load_n(&overflow_sequence,__ATOMIC_SEQ_CST) &&
       newer_sequence(sequence,consumed_sequence)) {
        consumed_sequence=sequence;
        Ps2MouseReport recovery{};recovery.buttons=buttons;
        dispatch_mouse_report(dev,recovery);
    }
}

bool mysz_zainicjalizowana = false;
static InputDevice s_ps2_mouse_device{};


/* =========================================================================
 * NISKIE I/O
 * ========================================================================= */

static inline void wyjscie_port_bajt(
    uint16_t port,
    uint8_t wartosc
) {
#ifdef BURSZTYN_PS2_HOST_TEST
    ps2_test_write(port,wartosc);
#else
    asm volatile(
        "outb %0, %1"
        :
        : "a"(wartosc), "Nd"(port)
        : "memory"
    );
#endif
}

static inline uint8_t wejscie_port_bajt(
    uint16_t port
) {
#ifdef BURSZTYN_PS2_HOST_TEST
    return ps2_test_read(port);
#else
    uint8_t wartosc = 0;

    asm volatile(
        "inb %1, %0"
        : "=a"(wartosc)
        : "Nd"(port)
        : "memory"
    );

    return wartosc;
#endif
}

/*
 * Krotkie opoznienie I/O przez port 0x80.
 * Na prawdziwym sprzecie ogranicza zbyt szybkie sekwencje dostepow
 * do starszego kontrolera 8042.
 */
static inline void opoznienie_io() {
#ifndef BURSZTYN_PS2_HOST_TEST
    asm volatile(
        "outb %%al, $0x80"
        :
        : "a"(0)
        : "memory"
    );
#endif
}

/* =========================================================================
 * OCZEKIWANIE NA KONTROLER
 * ========================================================================= */

bool czekaj_na_mozliwosc_zapisu() {
    for (uint32_t i = 0; i < LIMIT_OCZEKIWANIA; ++i) {
        const uint8_t status =
            wejscie_port_bajt(PORT_PS2_STATUS);

        if ((status & STATUS_INPUT_FULL) == 0) {
            return true;
        }

        asm volatile("pause");
    }

    return false;
}

bool czekaj_na_dane(
    bool wymagaj_danych_myszy,
    uint8_t* bajt_wyj
) {
    if (!bajt_wyj) return false;

    for (uint32_t i = 0; i < LIMIT_OCZEKIWANIA; ++i) {
        const uint8_t status =
            wejscie_port_bajt(PORT_PS2_STATUS);

        if ((status & STATUS_OUTPUT_FULL) == 0) {
            asm volatile("pause");
            continue;
        }

        /*
         * Jesli czekamy na odpowiedz myszy, nie wolno pomylic
         * scancode'u klawiatury z ACK myszy.
         *
         * Podczas inicjalizacji jadra przerwania sa wylaczone, dlatego
         * przekazujemy bajt klawiatury do jej dekodera, aby nie blokowal
         * odpowiedzi myszy i nie gubil legalnego nacisniecia klawisza.
         */
        const bool dane_myszy =
            (status & STATUS_AUX_DATA) != 0;

        const uint8_t bajt =
            wejscie_port_bajt(PORT_PS2_DANE);

        if(status & 0xC0U)continue;
        if(!wymagaj_danych_myszy && dane_myszy)continue;
        if (wymagaj_danych_myszy && !dane_myszy) {
            ps2_klawiatura_przetworz_scancode(bajt);
            continue;
        }

        *bajt_wyj = bajt;
        return true;
    }

    return false;
}

/*
 * Usuwa stare bajty oczekujace w buforze wyjsciowym kontrolera.
 * Funkcja jest uzywana tylko podczas inicjalizacji przed wlaczeniem IRQ.
 */
void oproznij_bufor_kontrolera() {
    for (uint32_t i = 0; i < 64U; ++i) {
        const uint8_t status =
            wejscie_port_bajt(PORT_PS2_STATUS);

        if ((status & STATUS_OUTPUT_FULL) == 0) {
            break;
        }

        const uint8_t b=wejscie_port_bajt(PORT_PS2_DANE);
        if(!(status & STATUS_AUX_DATA)) ps2_klawiatura_przetworz_scancode(b);
        opoznienie_io();
    }
}

/* =========================================================================
 * KOMENDY KONTROLERA 8042
 * ========================================================================= */

bool wyslij_komende_kontrolera(
    uint8_t komenda
) {
    if (!czekaj_na_mozliwosc_zapisu()) {
        return false;
    }

    wyjscie_port_bajt(
        PORT_PS2_CMD,
        komenda
    );

    opoznienie_io();
    return true;
}

bool zapisz_dane_kontrolera(
    uint8_t dane
) {
    if (!czekaj_na_mozliwosc_zapisu()) {
        return false;
    }

    wyjscie_port_bajt(
        PORT_PS2_DANE,
        dane
    );

    opoznienie_io();
    return true;
}

bool odczytaj_konfiguracje_kontrolera(
    uint8_t* konfiguracja
) {
    if (!konfiguracja) return false;

    if (!wyslij_komende_kontrolera(
            KONTROLER_CZYTAJ_KONFIG)) {
        return false;
    }

    /*
     * Odpowiedz 0x20 pochodzi od kontrolera, nie od myszy,
     * dlatego nie wymagamy bitu AUX.
     */
    return czekaj_na_dane(
        false,
        konfiguracja
    );
}

bool zapisz_konfiguracje_kontrolera(
    uint8_t konfiguracja
) {
    if (!wyslij_komende_kontrolera(
            KONTROLER_ZAPISZ_KONFIG)) {
        return false;
    }

    return zapisz_dane_kontrolera(
        konfiguracja
    );
}

/* =========================================================================
 * KOMENDY DO MYSZY
 * ========================================================================= */

bool wyslij_bajt_do_myszy_bez_ack(
    uint8_t bajt
) {
    if (!wyslij_komende_kontrolera(
            KONTROLER_DO_MYSZY)) {
        return false;
    }

    return zapisz_dane_kontrolera(
        bajt
    );
}

bool wyslij_komende_myszy(
    uint8_t komenda
) {
    /*
     * PS/2 moze odpowiedziec 0xFE (RESEND). Ponawiamy wtedy komende,
     * ale z ograniczona liczba prob.
     */
    for (uint32_t proba = 0;
         proba < LIMIT_PROB_RESEND;
         ++proba) {

        if (!wyslij_bajt_do_myszy_bez_ack(
                komenda)) {
            return false;
        }

        uint8_t odpowiedz = 0;

        if (!czekaj_na_dane(
                true,
                &odpowiedz)) {
            return false;
        }

        if (odpowiedz == MYSZ_ACK) {
            return true;
        }

        if (odpowiedz != MYSZ_RESEND) {
            return false;
        }
    }

    return false;
}

/* =========================================================================
 * OBRABIANIE PAKIETU MYSZY
 * ========================================================================= */

void zresetuj_synchronizacje_pakietu() { parser.reset(); }
void przyjmij_bajt_pakietu(uint8_t status,uint8_t bajt) {
    Ps2MouseReport r{};
    const bool complete=parser.feed(status,bajt,r);
#if BURSZTYNDIAGPS2
    const unsigned i=__atomic_load_n(&diag_written,__ATOMIC_RELAXED);
    if(i<128){packet_diags[i]={{bajt,parser.index,parser.bytes[0],parser.bytes[1],parser.bytes[2],parser.bytes[3],r.dx,r.dy,parser.overflow,parser.rejected}};
        __atomic_store_n(&diag_written,i+1,__ATOMIC_RELEASE);}
#endif
    if(!complete) return;
    const uint64_t sequence=++produced_sequence;
    const uint32_t head=__atomic_load_n(&pending_head,__ATOMIC_RELAXED), next=(head+1)%64;
    if(next==__atomic_load_n(&pending_tail,__ATOMIC_ACQUIRE)) {
        __atomic_store_n(&overflow_sequence,0,__ATOMIC_SEQ_CST);
        __atomic_store_n(&overflow_buttons,r.buttons,__ATOMIC_SEQ_CST);
        __atomic_store_n(&overflow_sequence,sequence,__ATOMIC_SEQ_CST);
        return;
    }
    pending[head]={r,sequence};__atomic_store_n(&pending_head,next,__ATOMIC_RELEASE);
}

} // namespace

/* =========================================================================
 * INICJALIZACJA MYSZY PS/2
 * ========================================================================= */

extern "C" void InicjalizujMyszPS2() {
    mysz_zainicjalizowana = false;
    parser.streaming=false;
    // Controlled startup/reinitialization runs with IRQs disabled.
    __atomic_store_n(&pending_head,0,__ATOMIC_RELAXED);
    __atomic_store_n(&pending_tail,0,__ATOMIC_RELAXED);
    __atomic_store_n(&overflow_sequence,0,__ATOMIC_RELAXED);
    consumed_sequence=produced_sequence;
    zresetuj_synchronizacje_pakietu();

    oproznij_bufor_kontrolera();

    /*
     * Wlacz drugi port kontrolera 8042.
     */
    if (!wyslij_komende_kontrolera(
            KONTROLER_WLACZ_PORT_MYSZY)) {
        return;
    }

    if(!wyslij_komende_kontrolera(0xAD))return;
    oproznij_bufor_kontrolera();
    uint8_t konfiguracja = 0;

    if (!odczytaj_konfiguracje_kontrolera(
            &konfiguracja)) {
        (void)wyslij_komende_kontrolera(0xAE);
        return;
    }

    (void)wyslij_komende_kontrolera(0xAE);
    konfiguracja &= uint8_t(~0x10U);

    /*
     * Wlacz IRQ12 i upewnij sie, ze zegar drugiego portu nie jest
     * zablokowany.
     *
     * Pozostale bity konfiguracji zachowujemy.
     */
    konfiguracja |=
        KONFIG_IRQ_MYSZY;

    konfiguracja &=
        static_cast<uint8_t>(
            ~KONFIG_ZABLOKUJ_ZEGAR_MYSZY);

    if (!zapisz_konfiguracje_kontrolera(
            konfiguracja)) {
        return;
    }

    /*
     * 0xF6 przywraca standardowe parametry:
     *   - 100 samples/s,
     *   - rozdzielczosc 4 counts/mm,
     *   - scaling 1:1.
     */
    if (!wyslij_komende_myszy(0xF5) || !wyslij_komende_myszy(
            MYSZ_USTAW_DOMYSLNE)) {
        return;
    }

    /*
     * Proba negocjacji IntelliMouse (obsluga 4-bajtowych pakietow z rolka):
     * Sekwencja probkowania magic knock: 200, 100, 80.
     */
    // Every byte is an acknowledged transaction. Never guess packet length
    // after a failed negotiation: leave the optional device unavailable.
    const uint8_t sequence[]={0xF3,200,0xF3,100,0xF3,80};
    for(uint8_t b:sequence) if(!wyslij_komende_myszy(b))return;
    uint8_t dev_id=0;
    if(!wyslij_komende_myszy(0xF2)||!czekaj_na_dane(true,&dev_id)||!parser.configure(dev_id))return;

    /*
     * 0xF4 uruchamia stream mode.
     */
    if (!wyslij_komende_myszy(
            MYSZ_WLACZ_RAPORTOWANIE)) {
        return;
    }

    parser.streaming=true;
    mysz_zainicjalizowana = true;

    if (!s_ps2_mouse_device.registered) {
        for (size_t i = 0; i < sizeof(s_ps2_mouse_device.name) - 1 && "ps2-mouse"[i]; ++i) {
            s_ps2_mouse_device.name[i] = "ps2-mouse"[i];
        }
        s_ps2_mouse_device.kinds = INPUT_DEVICE_KIND_MOUSE;
        s_ps2_mouse_device.bus_type = INPUT_BUS_PS2;
        s_ps2_mouse_device.device_id = parser.id;
        s_ps2_mouse_device.poll_fn=poll_mouse;
        input_register_device(&s_ps2_mouse_device);
    }
}

/* =========================================================================
 * IRQ12
 * ========================================================================= */

extern "C" void obsluga_przerwania_myszy() {
    /*
     * Audyt petli zrzutu bufora IRQ12:
     * 1. Bounded iteration count: scisle ograniczona do maks 64 iteracji, brak nieskonczonej petli
     * 2. Consumes only AUX bytes: odczyt portu 0x60 nastepuje wylacznie, gdy STATUS_AUX_DATA == 1
     * 3. Never consumes keyboard bytes: jesli bit AUX jest 0, petla natychmiast wychodzi bez czytania portu 0x60
     * 4. No unbounded ISR loop, no heap/sleep/blocking in ISR
     */
    for (uint32_t drain = 0; drain < 64U; ++drain) {
        const uint8_t status =
            wejscie_port_bajt(
                PORT_PS2_STATUS);

        /*
         * IRQ12 powinno wskazywac bajt myszy. Nie odczytujemy portu 0x60,
         * jesli bufor jest pusty albo zawiera bajt klawiatury.
         *
         * Jest to wazne, bo klawiatura i mysz dziela ten sam port danych.
         */
        if ((status & STATUS_OUTPUT_FULL) == 0) {
            break;
        }

        if ((status & STATUS_AUX_DATA) == 0) {
            break;
        }

        const uint8_t bajt =
            wejscie_port_bajt(
                PORT_PS2_DANE);

        if (!mysz_zainicjalizowana) {
            break;
        }

        przyjmij_bajt_pakietu(status,bajt);
    }

    /*
     * Brak EOI tutaj.
     *
     * WspolnaObslugaPrzerwan() w idt.cpp wykonuje EOI centralnie
     * po zakonczeniu handlera. Zapobiega to podwojnemu EOI oraz
     * zachowuje jedna polityke zakonczenia wszystkich IRQ APIC.
     */
}
