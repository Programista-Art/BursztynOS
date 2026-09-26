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
#include "input_device.h"

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
bool s_ps2_keys_down[256] = {};
static InputDevice s_ps2_keyboard_device{};

static void ps2_klawiatura_inicjalizuj_urzadzenie(void) {
    if (!s_ps2_keyboard_device.registered) {
        for (size_t i = 0; i < sizeof(s_ps2_keyboard_device.name) - 1 && "ps2-keyboard"[i]; ++i) {
            s_ps2_keyboard_device.name[i] = "ps2-keyboard"[i];
        }
        s_ps2_keyboard_device.kinds = INPUT_DEVICE_KIND_KEYBOARD;
        s_ps2_keyboard_device.bus_type = INPUT_BUS_PS2;
        s_ps2_keyboard_device.device_id = 0;
        input_register_device(&s_ps2_keyboard_device);
    }
}

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
#ifdef BURSZTYN_VIRTIO_HOST_TEST
    (void)port;
    return 0;
#else
    uint8_t wartosc = 0;

    asm volatile(
        "inb %1, %0"
        : "=a"(wartosc)
        : "Nd"(port));

    return wartosc;
#endif
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

        case 0x52: // Insert
            zglos_bajt_do_systemu(0x1B);
            zglos_bajt_do_systemu('[');
            zglos_bajt_do_systemu('2');
            zglos_bajt_do_systemu('~');
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

extern "C" void ps2_klawiatura_przetworz_scancode(uint8_t scancode) {
    /*
     * Kontynuacja sekwencji Pause/Break.
     */
    if (pozostalo_e1 != 0) {
        --pozostalo_e1;
        return;
    }

    if (scancode == SC_E1) {
        pozostalo_e1 = E1_BAJTY_DO_POMINIECIA;
        klawisz_rozszerzony_e0 = false;
        return;
    }

    /*
     * E0 nie jest samodzielnym klawiszem.
     */
    if (scancode == SC_E0) {
        klawisz_rozszerzony_e0 = true;
        return;
    }

    const bool is_e0 = klawisz_rozszerzony_e0;
    klawisz_rozszerzony_e0 = false;

    const bool down = (scancode & SC_RELEASE_BIT) == 0;
    const uint16_t linux_kc = input_ps2_scancode_to_linux(is_e0, scancode);
    if (linux_kc == 0) {
        return;
    }

    uint32_t val = 0;
    if (down) {
        if (linux_kc < 256 && s_ps2_keys_down[linux_kc]) {
            val = 2; // Klawisz wcisniety ponownie bez zwolnienia = repeat
        } else {
            if (linux_kc < 256) s_ps2_keys_down[linux_kc] = true;
            val = 1; // Nowe wcisniecie = press
        }
    } else {
        if (linux_kc < 256) {
            if (!s_ps2_keys_down[linux_kc]) {
                return; // Brak duplikatu zwolnienia
            }
            s_ps2_keys_down[linux_kc] = false;
        }
        val = 0; // Zwolnienie = release
    }

    ps2_klawiatura_inicjalizuj_urzadzenie();

    InputEvent evs[2] = {
        { INPUT_EV_KEY, linux_kc, val },
        { INPUT_EV_SYN, INPUT_SYN_REPORT, 0 }
    };
    input_dispatch_events(&s_ps2_keyboard_device, evs, 2);
}

extern "C" void ps2_klawiatura_zresetuj_stan(void) {
    klawisz_rozszerzony_e0 = false;
    pozostalo_e1 = 0;
    for (size_t i = 0; i < 256; ++i) {
        s_ps2_keys_down[i] = false;
    }
}

extern "C" InputDevice* ps2_klawiatura_pobierz_urzadzenie(void) {
    ps2_klawiatura_inicjalizuj_urzadzenie();
    return &s_ps2_keyboard_device;
}

extern "C" void obsluga_przerwania_klawiatury() {
    /*
     * Przy poprawnym IRQ1 bit OBF powinien byc ustawiony. Kontrola chroni
     * przed odczytem pustego portu przy nietypowym/spurious wywolaniu.
     */
    const uint8_t status = wejscie_port_bajt(PORT_PS2_STATUS);
    if ((status & PS2_STATUS_OUTPUT_FULL) == 0) {
        return;
    }

    const uint8_t scancode = wejscie_port_bajt(PORT_PS2_DANE);
    ps2_klawiatura_przetworz_scancode(scancode);
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

/*
 * Wspolny punkt wejscia dla USB HID Boot Keyboard (dla zachowania kompatybilnosci).
 * Faktyczne parsowanie raportu USB odbywa sie w sterowniku xHCI/HID (usb_hid.cpp),
 * ktory nastepnie przekazuje zdarzenia klawiszy przez jednolita abstrakcje InputDevice.
 */
extern "C" void usb_hid_przetworz_raport(const uint8_t* raport);

extern "C" void usb_wprowadz_raport_klawiatury(const uint8_t* raport) {
    usb_hid_przetworz_raport(raport);
}

extern "C" void klawiatura_zresetuj_stan(void) {
    lewy_shift = false;
    prawy_shift = false;
    prawy_alt = false;
    caps_lock = false;
    lewy_ctrl = false;
    prawy_ctrl = false;
    lewy_alt = false;
    lewy_super = false;
    prawy_super = false;
    num_lock = false;
    usb_modyfikatory = 0;
    klawisz_rozszerzony_e0 = false;
    pozostalo_e1 = 0;
    __atomic_store_n(&bufor_glowa, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&bufor_ogon, 0, __ATOMIC_RELEASE);
    ps2_klawiatura_zresetuj_stan();
}

#ifdef BURSZTYN_VIRTIO_HOST_TEST
extern "C" void klawiatura_test_hook(uint16_t keycode, uint32_t value) __attribute__((weak));
#endif

extern "C" void klawiatura_obsluz_klawisz(uint16_t keycode, uint32_t value) {
#ifdef BURSZTYN_VIRTIO_HOST_TEST
    if (klawiatura_test_hook) {
        klawiatura_test_hook(keycode, value);
    }
#endif
    const bool down = (value != 0);

    // 1. Klawisze modyfikatorow
    switch (keycode) {
        case INPUT_KEY_LEFTSHIFT: // 42
            lewy_shift = down;
            zglos_fizyczny(SC_LSHIFT_DOWN, down);
            return;
        case INPUT_KEY_RIGHTSHIFT: // 54
            prawy_shift = down;
            zglos_fizyczny(SC_RSHIFT_DOWN, down);
            return;
        case INPUT_KEY_LEFTCTRL: // 29
            lewy_ctrl = down;
            zglos_fizyczny(SC_LCTRL_DOWN, down);
            return;
        case INPUT_KEY_RIGHTCTRL: // 97
            prawy_ctrl = down;
            zglos_fizyczny(BWS_KEY_SOURCE_E0 | 0x1DU, down);
            return;
        case INPUT_KEY_LEFTALT: // 56
            lewy_alt = down;
            zglos_fizyczny(SC_LALT_DOWN, down);
            return;
        case INPUT_KEY_RIGHTALT: // 100 (AltGr)
            prawy_alt = down;
            zglos_fizyczny(BWS_KEY_SOURCE_E0 | 0x38U, down);
            return;
        case INPUT_KEY_LEFTMETA: // 125
            lewy_super = down;
            zglos_fizyczny(BWS_KEY_SOURCE_E0 | 0x5BU, down);
            return;
        case INPUT_KEY_RIGHTMETA: // 126
            prawy_super = down;
            zglos_fizyczny(BWS_KEY_SOURCE_E0 | 0x5CU, down);
            return;
        case INPUT_KEY_CAPSLOCK: // 58
            if (value == 1) caps_lock = !caps_lock;
            zglos_fizyczny(SC_CAPSLOCK, down);
            return;
        case INPUT_KEY_NUMLOCK: // 69
            if (value == 1) num_lock = !num_lock;
            zglos_fizyczny(SC_NUMLOCK, down);
            return;
        case INPUT_KEY_SCROLLLOCK: // 70
            zglos_fizyczny(0x46, down);
            return;
        default:
            break;
    }

    // 2. Klawisze rozszerzone (E0 w protokole PS/2)
    uint8_t e0_scancode = 0;
    switch (keycode) {
        case INPUT_KEY_UP:       e0_scancode = 0x48; break;
        case INPUT_KEY_DOWN:     e0_scancode = 0x50; break;
        case INPUT_KEY_LEFT:     e0_scancode = 0x4B; break;
        case INPUT_KEY_RIGHT:    e0_scancode = 0x4D; break;
        case INPUT_KEY_HOME:     e0_scancode = 0x47; break;
        case INPUT_KEY_END:      e0_scancode = 0x4F; break;
        case INPUT_KEY_PAGEUP:   e0_scancode = 0x49; break;
        case INPUT_KEY_PAGEDOWN: e0_scancode = 0x51; break;
        case INPUT_KEY_INSERT:   e0_scancode = 0x52; break;
        case INPUT_KEY_DELETE:   e0_scancode = 0x53; break;
        case INPUT_KEY_KPENTER:  e0_scancode = 0x1C; break;
        case INPUT_KEY_KPSLASH:  e0_scancode = 0x35; break;
        default: break;
    }

    if (e0_scancode != 0) {
        obsluz_e0(down ? e0_scancode : static_cast<uint8_t>(e0_scancode | SC_RELEASE_BIT));
        return;
    }

    // 3. Standardowe klawisze Scan Code Set 1 (kody 1..88)
    if (keycode > 0 && keycode < 128) {
        const uint8_t scancode = static_cast<uint8_t>(keycode);
        zglos_fizyczny(scancode, down);
        if (down) {
            obsluz_zwykly_make_code(scancode);
        }
    }
}
