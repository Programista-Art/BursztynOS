/*
 * Bursztyn OS - sterownik klawiatury PS/2 (Scan Code Set 1)
 *
 * Obsluguje:
 *  - standardowe znaki ASCII,
 *  - lewy i prawy Shift,
 *  - Caps Lock,
 *  - prawy Alt (AltGr) przez prefiks E0,
 *  - polskie znaki UTF-8,
 *  - jednokierunkowy bufor znakow dla aplikacji Ring 3.
 *
 * EOI Local APIC NIE jest wysylane w tym pliku.
 * Odpowiada za nie wspolny dispatcher IDT.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "bws_zdarzenia.h"

/* =========================================================================
 * PORTY PS/2
 * ========================================================================= */

namespace {

constexpr uint16_t PORT_PS2_DANE = 0x60;
constexpr uint16_t PORT_PS2_STATUS = 0x64;

constexpr uint8_t PS2_STATUS_OUTPUT_FULL = 1U << 0;

/* =========================================================================
 * SCAN CODE SET 1
 * ========================================================================= */

constexpr uint8_t SC_E0 = 0xE0;
constexpr uint8_t SC_E1 = 0xE1;

constexpr uint8_t SC_LSHIFT_DOWN = 0x2A;
constexpr uint8_t SC_LSHIFT_UP   = 0xAA;

constexpr uint8_t SC_RSHIFT_DOWN = 0x36;
constexpr uint8_t SC_RSHIFT_UP   = 0xB6;

constexpr uint8_t SC_CAPSLOCK = 0x3A;
constexpr uint8_t SC_NUMLOCK = 0x45;
constexpr uint8_t SC_LCTRL_DOWN = 0x1D;
constexpr uint8_t SC_LCTRL_UP = 0x9D;
constexpr uint8_t SC_LALT_DOWN = 0x38;
constexpr uint8_t SC_LALT_UP = 0xB8;

constexpr uint8_t SC_RALT_DOWN_E0 = 0x38;
constexpr uint8_t SC_RALT_UP_E0   = 0xB8;

constexpr uint8_t SC_RELEASE_BIT = 0x80;

/*
 * Pause/Break zaczyna sie od E1 i ma wielobajtowa sekwencje.
 * Na obecnym etapie Bursztyn jej nie wykorzystuje.
 */
constexpr uint8_t E1_BAJTY_DO_POMINIECIA = 5;

/* =========================================================================
 * BUFOR KLAWIATURY
 * ========================================================================= */

constexpr uint32_t ROZMIAR_BUFORA = 256;

/*
 * Ring buffer ma faktyczna pojemnosc 255 bajtow.
 * Jedna pozycja pozostaje pusta, aby rozroznic:
 *   glowa == ogon  -> pusty
 *   next(glowa)==ogon -> pelny
 *
 * ISR jest jedynym producentem, a pobierz_znak_klawiatury()
 * jedynym konsumentem. Indeksy sa publikowane atomowo.
 */
char bufor_klawiatury[ROZMIAR_BUFORA] = {};

uint32_t bufor_glowa = 0;
uint32_t bufor_ogon = 0;

/*
 * Liczba odrzuconych bajtow moze pozniej posluzyc do diagnostyki.
 * Nie blokujemy ISR, gdy bufor jest pelny.
 */
uint64_t licznik_przepelnien_bufora = 0;

/* =========================================================================
 * STAN MODYFIKATOROW
 * ========================================================================= */

bool lewy_shift = false;
bool prawy_shift = false;
bool prawy_alt = false;
bool caps_lock = false;
bool lewy_ctrl = false;
bool prawy_ctrl = false;
bool lewy_alt = false;
bool lewy_super = false;
bool prawy_super = false;
bool num_lock = false;
uint32_t usb_modyfikatory = 0;

bool klawisz_rozszerzony_e0 = false;
uint8_t pozostalo_e1 = 0;

/* =========================================================================
 * MAPOWANIE KLAWISZY
 * ========================================================================= */

const char kbd_us[128] = {
    0,    27,  '1',  '2',  '3',  '4',  '5',  '6',
    '7',  '8', '9',  '0',  '-',  '=',  '\b', '\t',

    'q',  'w', 'e',  'r',  't',  'y',  'u',  'i',
    'o',  'p', '[',  ']',  '\n', 0,    'a',  's',

    'd',  'f', 'g',  'h',  'j',  'k',  'l',  ';',
    '\'', '`', 0,    '\\',  'z',  'x',  'c',  'v',

    'b',  'n', 'm',  ',',  '.',  '/',  0,    '*',
    0,    ' ', 0,    0,    0,    0,    0,    0,

    0,    0,   0,    0,    0,    0,    0,    '7',
    '8',  '9', '-',  '4',  '5',  '6',  '+',  '1',

    '2',  '3', '0',  '.',  0,    0,    0,    0,
    0,    0,   0,    0,    0,    0,    0,    0,

    0,    0,   0,    0,    0,    0,    0,    0,
    0,    0,   0,    0,    0,    0,    0,    0,

    0,    0,   0,    0,    0,    0,    0,    0,
    0,    0,   0,    0,    0,    0,    0,    0
};

const char kbd_us_shift[128] = {
    0,    27,  '!',  '@',  '#',  '$',  '%',  '^',
    '&',  '*', '(',  ')',  '_',  '+',  '\b', '\t',

    'Q',  'W', 'E',  'R',  'T',  'Y',  'U',  'I',
    'O',  'P', '{',  '}',  '\n', 0,    'A',  'S',

    'D',  'F', 'G',  'H',  'J',  'K',  'L',  ':',
    '"',  '~', 0,    '|',   'Z',  'X',  'C',  'V',

    'B',  'N', 'M',  '<',  '>',  '?',  0,    '*',
    0,    ' ', 0,    0,    0,    0,    0,    0,

    0,    0,   0,    0,    0,    0,    0,    '7',
    '8',  '9', '-',  '4',  '5',  '6',  '+',  '1',

    '2',  '3', '0',  '.',  0,    0,    0,    0,
    0,    0,   0,    0,    0,    0,    0,    0,

    0,    0,   0,    0,    0,    0,    0,    0,
    0,    0,   0,    0,    0,    0,    0,    0,

    0,    0,   0,    0,    0,    0,    0,    0,
    0,    0,   0,    0,    0,    0,    0,    0
};

/* =========================================================================
 * NISKIE I/O
 * ========================================================================= */

static inline uint8_t wejscie_port_bajt(uint16_t port) {
    uint8_t wartosc = 0;

    asm volatile(
        "inb %1, %0"
        : "=a"(wartosc)
        : "Nd"(port));

    return wartosc;
}

/* =========================================================================
 * GUI
 * ========================================================================= */

extern "C" bool zaktualizuj_klawiature_gui(char znak);
extern "C" bool zaktualizuj_klawisz_fizyczny_gui(
    uint32_t kod, bool wcisniety, uint32_t modyfikatory);
extern "C" bool zaktualizuj_tekst_gui(
    uint32_t codepoint, uint32_t modyfikatory);
extern "C" void scheduler_wybudz_klawiature();

uint32_t aktualne_modyfikatory() {
    uint32_t m = 0;
    if (lewy_shift || prawy_shift) m |= BWS_MOD_SHIFT;
    if (lewy_ctrl || prawy_ctrl) m |= BWS_MOD_CTRL;
    if (lewy_alt || prawy_alt) m |= BWS_MOD_ALT;
    if (lewy_super || prawy_super) m |= BWS_MOD_SUPER;
    if (caps_lock) m |= BWS_MOD_CAPS;
    if (num_lock) m |= BWS_MOD_NUM;
    return m | usb_modyfikatory;
}

void zglos_fizyczny(uint32_t kod, bool down) {
    (void)zaktualizuj_klawisz_fizyczny_gui(
        kod, down, aktualne_modyfikatory());
}

/* =========================================================================
 * POMOCNICZE OPERACJE BUFORA
 * ========================================================================= */

static inline uint32_t nastepny_indeks(uint32_t indeks) {
    return (indeks + 1U) % ROZMIAR_BUFORA;
}

bool dodaj_bajt_do_bufora(uint8_t bajt) {
    const uint32_t glowa =
        __atomic_load_n(
            &bufor_glowa,
            __ATOMIC_RELAXED);

    const uint32_t ogon =
        __atomic_load_n(
            &bufor_ogon,
            __ATOMIC_ACQUIRE);

    const uint32_t nowa_glowa =
        nastepny_indeks(glowa);

    if (nowa_glowa == ogon) {
        __atomic_add_fetch(
            &licznik_przepelnien_bufora,
            1ULL,
            __ATOMIC_RELAXED);

        return false;
    }

    bufor_klawiatury[glowa] =
        static_cast<char>(bajt);

    /*
     * Publikujemy indeks dopiero po zapisaniu bajtu.
     */
    __atomic_store_n(
        &bufor_glowa,
        nowa_glowa,
        __ATOMIC_RELEASE);

    return true;
}

/*
 * Najpierw dajemy znak aktywnemu odbiorcy GUI.
 * Jezeli GUI go nie przejmie, trafia do kolejki terminala/BWS.
 */
void zglos_bajt_do_systemu(uint8_t bajt) {
    if (zaktualizuj_klawiature_gui(
            static_cast<char>(bajt))) {
        return;
    }

    if(dodaj_bajt_do_bufora(bajt))
        scheduler_wybudz_klawiature();
}

/* =========================================================================
 * UTF-8
 * ========================================================================= */

void zglos_utf8_2(uint8_t b1,
                  uint8_t b2) {
    /*
     * Para UTF-8 jest logicznie jednym znakiem. Ring buffer jest jednak
     * bajtowy, zgodnie z dotychczasowym API Bursztyna.
     */
    const uint32_t codepoint =
        (static_cast<uint32_t>(b1 & 0x1FU) << 6U) |
        static_cast<uint32_t>(b2 & 0x3FU);
    (void)zaktualizuj_tekst_gui(codepoint, aktualne_modyfikatory());
    zglos_bajt_do_systemu(b1);
    zglos_bajt_do_systemu(b2);
}

bool obsluz_polski_znak(uint8_t scancode,
                        bool wielka_litera) {
    switch (scancode) {
        case 0x1E: // A -> a/ą
            zglos_utf8_2(
                0xC4,
                wielka_litera ? 0x84 : 0x85); // Ą / ą
            return true;

        case 0x2E: // C -> c/ć
            zglos_utf8_2(
                0xC4,
                wielka_litera ? 0x86 : 0x87); // Ć / ć
            return true;

        case 0x12: // E -> e/ę
            zglos_utf8_2(
                0xC4,
                wielka_litera ? 0x98 : 0x99); // Ę / ę
            return true;

        case 0x26: // L -> l/ł
            zglos_utf8_2(
                0xC5,
                wielka_litera ? 0x81 : 0x82); // Ł / ł
            return true;

        case 0x31: // N -> n/ń
            zglos_utf8_2(
                0xC5,
                wielka_litera ? 0x83 : 0x84); // Ń / ń
            return true;

        case 0x18: // O -> o/ó
            zglos_utf8_2(
                0xC3,
                wielka_litera ? 0x93 : 0xB3); // Ó / ó
            return true;

        case 0x1F: // S -> s/ś
            zglos_utf8_2(
                0xC5,
                wielka_litera ? 0x9A : 0x9B); // Ś / ś
            return true;

        case 0x2D: // X -> x/ź
            zglos_utf8_2(
                0xC5,
                wielka_litera ? 0xB9 : 0xBA); // Ź / ź
            return true;

        case 0x2C: // Z -> z/ż
            zglos_utf8_2(
                0xC5,
                wielka_litera ? 0xBB : 0xBC); // Ż / ż
            return true;

        default:
            return false;
    }
}

/* =========================================================================
 * ZWYKLE ZNAKI ASCII
 * ========================================================================= */

bool czy_litera_ascii(char znak) {
    return
        (znak >= 'a' && znak <= 'z') ||
        (znak >= 'A' && znak <= 'Z');
}

char zastosuj_caps_lock(char znak,
                        bool shift) {
    if (!caps_lock ||
        !czy_litera_ascii(znak)) {
        return znak;
    }

    /*
     * Dla liter Caps Lock odwraca znaczenie Shift:
     *  caps=1 shift=0 -> wielka
     *  caps=1 shift=1 -> mala
     */
    if (shift) {
        if (znak >= 'A' && znak <= 'Z')
            return static_cast<char>(
                znak - 'A' + 'a');
    } else {
        if (znak >= 'a' && znak <= 'z')
            return static_cast<char>(
                znak - 'a' + 'A');
    }

    return znak;
}

void obsluz_zwykly_make_code(uint8_t scancode) {
    if (scancode >= 128)
        return;

    const bool shift =
        lewy_shift || prawy_shift;

    /*
     * Dla polskich znakow wielkosc litery jest zgodna z kombinacja
     * Shift XOR Caps Lock, tak samo jak dla zwyklych liter.
     */
    if (prawy_alt) {
        const bool wielka_litera =
            shift ^ caps_lock;

        if (obsluz_polski_znak(
                scancode,
                wielka_litera)) {
            return;
        }

        /*
         * Dla obecnego layoutu AltGr + inny klawisz nie generuje znaku.
         * Zapobiega to przypadkowemu wpisywaniu zwyklego znaku podczas
         * trzymania prawego Alt.
         */
        return;
    }

    char znak =
        shift ?
        kbd_us_shift[scancode] :
        kbd_us[scancode];

    if (znak == 0)
        return;

    znak =
        zastosuj_caps_lock(
            znak,
            shift);

    (void)zaktualizuj_tekst_gui(
        static_cast<uint8_t>(znak), aktualne_modyfikatory());
    zglos_bajt_do_systemu(
        static_cast<uint8_t>(znak));
}

/* =========================================================================
 * E0 - KLAWISZE ROZSZERZONE
 * ========================================================================= */

void obsluz_e0(uint8_t scancode) {
    /*
     * Prawy Alt / AltGr.
     */
    if (scancode == SC_RALT_DOWN_E0) {
        prawy_alt = true;
        zglos_fizyczny(BWS_KEY_SOURCE_E0 | 0x38U, true);
        return;
    }

    if (scancode == SC_RALT_UP_E0) {
        prawy_alt = false;
        zglos_fizyczny(BWS_KEY_SOURCE_E0 | 0x38U, false);
        return;
    }

    if (scancode == 0x1D) { prawy_ctrl = true;
        zglos_fizyczny(BWS_KEY_SOURCE_E0 | 0x1DU, true); return; }
    if (scancode == 0x9D) { prawy_ctrl = false;
        zglos_fizyczny(BWS_KEY_SOURCE_E0 | 0x1DU, false); return; }
    if (scancode == 0x5B || scancode == 0x5C) {
        if (scancode == 0x5B) lewy_super = true; else prawy_super = true;
        zglos_fizyczny(BWS_KEY_SOURCE_E0 | scancode, true); return;
    }
    if (scancode == 0xDB || scancode == 0xDC) {
        if (scancode == 0xDB) lewy_super = false; else prawy_super = false;
        zglos_fizyczny(BWS_KEY_SOURCE_E0 | (scancode & 0x7FU), false); return;
    }

    /*
     * Podstawowe sekwencje ANSI dla strzalek.
     *
     * Dzięki temu shell / edytor tekstu moze w przyszlosci obslugiwac
     * je bez rozszerzania aktualnego bajtowego API klawiatury.
     */
    zglos_fizyczny(BWS_KEY_SOURCE_E0 | (scancode & 0x7FU),
                   (scancode & SC_RELEASE_BIT) == 0);
    if ((scancode & SC_RELEASE_BIT) != 0)
        return;

    switch (scancode) {
        case 0x48: // gora
            zglos_bajt_do_systemu(0x1B);
            zglos_bajt_do_systemu('[');
            zglos_bajt_do_systemu('A');
            break;

        case 0x50: // dol
            zglos_bajt_do_systemu(0x1B);
            zglos_bajt_do_systemu('[');
            zglos_bajt_do_systemu('B');
            break;

        case 0x4D: // prawo
            zglos_bajt_do_systemu(0x1B);
            zglos_bajt_do_systemu('[');
            zglos_bajt_do_systemu('C');
            break;

        case 0x4B: // lewo
            zglos_bajt_do_systemu(0x1B);
            zglos_bajt_do_systemu('[');
            zglos_bajt_do_systemu('D');
            break;

        case 0x53: // Delete
            zglos_bajt_do_systemu(0x1B);
            zglos_bajt_do_systemu('[');
            zglos_bajt_do_systemu('3');
            zglos_bajt_do_systemu('~');
            break;

        case 0x49: // PageUp
            zglos_bajt_do_systemu(0x1B);
            zglos_bajt_do_systemu('[');
            zglos_bajt_do_systemu('5');
            zglos_bajt_do_systemu('~');
            break;

        case 0x51: // PageDown
            zglos_bajt_do_systemu(0x1B);
            zglos_bajt_do_systemu('[');
            zglos_bajt_do_systemu('6');
            zglos_bajt_do_systemu('~');
            break;

        case 0x47: // Home
            zglos_bajt_do_systemu(0x1B);
            zglos_bajt_do_systemu('[');
            zglos_bajt_do_systemu('H');
            break;

        case 0x4F: // End
            zglos_bajt_do_systemu(0x1B);
            zglos_bajt_do_systemu('[');
            zglos_bajt_do_systemu('F');
            break;

        case 0x1C: // Enter z klawiatury numerycznej
            zglos_bajt_do_systemu('\n');
            break;

        case 0x35: // Slash z klawiatury numerycznej
            zglos_bajt_do_systemu('/');
            break;

        default:
            break;
    }
}

/* =========================================================================
 * ISR
 * ========================================================================= */

} // namespace

extern "C" void obsluga_przerwania_klawiatury() {
    /*
     * Przy poprawnym IRQ1 bit OBF powinien byc ustawiony. Kontrola chroni
     * przed odczytem pustego portu przy nietypowym/spurious wywolaniu.
     */
    const uint8_t status =
        wejscie_port_bajt(
            PORT_PS2_STATUS);

    if ((status & PS2_STATUS_OUTPUT_FULL) == 0) {
        return;
    }

    const uint8_t scancode =
        wejscie_port_bajt(
            PORT_PS2_DANE);

    /*
     * Kontynuacja sekwencji Pause/Break.
     */
    if (pozostalo_e1 != 0) {
        --pozostalo_e1;
        return;
    }

    if (scancode == SC_E1) {
        pozostalo_e1 =
            E1_BAJTY_DO_POMINIECIA;

        klawisz_rozszerzony_e0 = false;
        return;
    }

    /*
     * E0 nie jest samodzielnym klawiszem.
     * IDT wysle EOI po wyjsciu z handlera.
     */
    if (scancode == SC_E0) {
        klawisz_rozszerzony_e0 = true;
        return;
    }

    if (klawisz_rozszerzony_e0) {
        klawisz_rozszerzony_e0 = false;
        obsluz_e0(scancode);
        return;
    }

    /*
     * Modyfikatory standardowego zestawu Scan Code Set 1.
     */
    switch (scancode) {
        case SC_LSHIFT_DOWN:
            lewy_shift = true;
            zglos_fizyczny(SC_LSHIFT_DOWN, true);
            return;

        case SC_LSHIFT_UP:
            lewy_shift = false;
            zglos_fizyczny(SC_LSHIFT_DOWN, false);
            return;

        case SC_RSHIFT_DOWN:
            prawy_shift = true;
            zglos_fizyczny(SC_RSHIFT_DOWN, true);
            return;

        case SC_RSHIFT_UP:
            prawy_shift = false;
            zglos_fizyczny(SC_RSHIFT_DOWN, false);
            return;

        case SC_CAPSLOCK:
            caps_lock = !caps_lock;
            zglos_fizyczny(SC_CAPSLOCK, true);
            return;

        case SC_NUMLOCK:
            num_lock = !num_lock;
            zglos_fizyczny(SC_NUMLOCK, true);
            return;

        case SC_LCTRL_DOWN:
            lewy_ctrl = true;
            zglos_fizyczny(SC_LCTRL_DOWN, true);
            return;

        case SC_LCTRL_UP:
            lewy_ctrl = false;
            zglos_fizyczny(SC_LCTRL_DOWN, false);
            return;

        case SC_LALT_DOWN:
            lewy_alt = true;
            zglos_fizyczny(SC_LALT_DOWN, true);
            return;

        case SC_LALT_UP:
            lewy_alt = false;
            zglos_fizyczny(SC_LALT_DOWN, false);
            return;

        default:
            break;
    }

    /*
     * Break code zwyklego klawisza.
     */
    zglos_fizyczny(scancode & 0x7FU, (scancode & SC_RELEASE_BIT) == 0);
    if ((scancode & SC_RELEASE_BIT) != 0)
        return;

    obsluz_zwykly_make_code(
        scancode);

    /*
     * Brak EOI tutaj.
     * WspolnaObslugaPrzerwan() w idt.cpp wykonuje je centralnie
     * i sprawdza ISR Local APIC, aby uniknac podwojnego EOI.
     */
}

extern "C" uint32_t klawiatura_modyfikatory() {
    return aktualne_modyfikatory();
}

/* =========================================================================
 * BWS - ODCZYT KOLEJNEGO BAJTU
 * ========================================================================= */

extern "C" char pobierz_znak_klawiatury() {
    const uint32_t ogon =
        __atomic_load_n(
            &bufor_ogon,
            __ATOMIC_RELAXED);

    const uint32_t glowa =
        __atomic_load_n(
            &bufor_glowa,
            __ATOMIC_ACQUIRE);

    if (ogon == glowa)
        return 0;

    const char znak =
        bufor_klawiatury[ogon];

    const uint32_t nowy_ogon =
        nastepny_indeks(ogon);

    /*
     * Publikujemy zwolnienie pozycji dopiero po odczytaniu bajtu.
     */
    __atomic_store_n(
        &bufor_ogon,
        nowy_ogon,
        __ATOMIC_RELEASE);

    return znak;
}

/* Wspolny punkt wejscia dla USB HID Boot Keyboard. USB przekazuje Usage ID,
 * nigdy skan-kod PS/2; tutaj oba zrodla koncza w tej samej kolejce znakow. */
extern "C" void usb_wprowadz_raport_klawiatury(const uint8_t* raport) {
    if (!raport) return;
    static uint8_t poprzedni_raport[8]={};
    static bool usb_caps = false;
    const uint8_t raw_mod = raport[0];
    uint32_t mods = usb_caps ? BWS_MOD_CAPS : 0U;
    if (raw_mod & 0x22U) mods |= BWS_MOD_SHIFT;
    if (raw_mod & 0x11U) mods |= BWS_MOD_CTRL;
    if (raw_mod & 0x44U) mods |= BWS_MOD_ALT;
    if (raw_mod & 0x88U) mods |= BWS_MOD_SUPER;
    usb_modyfikatory = mods;
    for (uint8_t bit=0; bit<8; ++bit) {
        const bool was=(poprzedni_raport[0]&(1U<<bit))!=0;
        const bool now=(raw_mod&(1U<<bit))!=0;
        if(was!=now)(void)zaktualizuj_klawisz_fizyczny_gui(
            BWS_KEY_SOURCE_USB | static_cast<uint32_t>(224U+bit), now, mods);
    }
    for (uint8_t i=0;i<6;++i) {
        const uint8_t old=poprzedni_raport[2+i]; if(!old||old==1U)continue;
        bool nadal=false;for(uint8_t j=0;j<6;++j)if(raport[2+j]==old)nadal=true;
        if(!nadal)(void)zaktualizuj_klawisz_fizyczny_gui(
            BWS_KEY_SOURCE_USB|old,false,mods);
    }
    for (uint8_t i=0;i<6;++i) {
        const uint8_t u=raport[2+i]; if (!u||u==1U) continue;
        bool poprzedni=false;
        for (uint8_t j=0;j<6;++j) if (poprzedni_raport[2+j]==u) poprzedni=true;
        if (poprzedni) continue;
        (void)zaktualizuj_klawisz_fizyczny_gui(
            BWS_KEY_SOURCE_USB|u,true,mods);
        if (u == 57U) {
            usb_caps = !usb_caps;
            if (usb_caps)
                mods |= BWS_MOD_CAPS;
            else
                mods &= ~BWS_MOD_CAPS;
            usb_modyfikatory = mods;
            continue;
        }
        const bool przesuniecie=(mods&BWS_MOD_SHIFT)!=0;
        char znak=0;
        if(u>=4U&&u<=29U){const bool upper=przesuniecie^usb_caps;
            znak=static_cast<char>((upper?'A':'a')+(u-4U));}
        else if(u>=30U&&u<=39U){static const char zwykle[]="1234567890";static const char sz[]="!@#$%^&*()";znak=(przesuniecie?sz:zwykle)[u-30U];}
        else if(u==40U)znak='\n'; else if(u==41U)znak=27; else if(u==42U)znak='\b';
        else if(u==43U)znak='\t'; else if(u==44U)znak=' ';
        else if(u==45U)znak=przesuniecie?'_':'-'; else if(u==46U)znak=przesuniecie?'+':'=';
        else if(u==51U)znak=przesuniecie?':':';'; else if(u==52U)znak=przesuniecie?'"':'\'';
        else if(u==53U)znak=przesuniecie?'~':'`'; else if(u==54U)znak=przesuniecie?'<':',';
        else if(u==55U)znak=przesuniecie?'>':'.'; else if(u==56U)znak=przesuniecie?'?':'/';
        if(znak){(void)zaktualizuj_tekst_gui(
            static_cast<uint8_t>(znak),mods);
            zglos_bajt_do_systemu(static_cast<uint8_t>(znak));}
    }
    for(uint8_t i=0;i<8;++i)poprzedni_raport[i]=raport[i];
}
