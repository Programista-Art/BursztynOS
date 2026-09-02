/*
 * Bursztyn OS - glowny punkt wejscia jadra x86_64.
 *
 * Kolejnosc startu:
 *  1. PMM / GDT / TSS / IDT / VMM
 *  2. BWS i grafika
 *  3. sterta jadra i scheduler
 *  4. APIC / PS2 / PCI / sterowniki
 *  5. PSF i instalacja wbudowanych aplikacji
 *  6. utworzenie procesu Menedzera Okien
 *  7. wlaczenie wielozadaniowosci i przerwan
 *
 * Przerwania pozostaja wylaczone przez cala faze inicjalizacji.
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "pamiec.h"
#include "psf.h"
#include "grafika.h"
#include "loader.h"
#include "ahci.h"
#include "e1000.h"
#include "siec.h"
#include "heap.h"
#include "scheduler.h"
#include "skladacz_obrazu.h"
#include "acpi.h"
#include "sterowniki/czas/hpet.h"
#include "sterowniki/dzwiek/hda.h"
#include "sterowniki/usb/usb.h"

#ifndef BURSZTYN_DEBUG_NET_SELFTEST
#define BURSZTYN_DEBUG_NET_SELFTEST 0
#endif

#ifndef BURSZTYN_DEBUG_PSF_SELFTEST
#define BURSZTYN_DEBUG_PSF_SELFTEST 0
#endif

#ifndef BURSZTYN_TESTY_PSF
#define BURSZTYN_TESTY_PSF 0
#endif

#if BURSZTYN_DEBUG_NET_SELFTEST
static void wykonaj_selftest_sieci() {
    const uint32_t gateway = bws_siec_ping(10, 0, 2, 2);
    wypisz_log(gateway ? "[NET-TEST] ping 10.0.2.2 send: OK" :
                         "[NET-TEST] ping 10.0.2.2 send: BLAD");
    const uint32_t dns = bws_siec_ping(10, 0, 2, 3);
    wypisz_log(dns ? "[NET-TEST] ping 10.0.2.3 send: OK" :
                     "[NET-TEST] ping 10.0.2.3 send: BLAD");
    if (gateway && dns) {
        uint8_t ip[4] = {};
        const bool dns_ok = kernel_siec_dns("example.com", ip);
        wypisz_log(dns_ok ? "[NET-TEST] DNS example.com: OK" :
                           "[NET-TEST] DNS example.com: BLAD");
        if (dns_ok) {
            static char odpowiedz[32U * 1024U];
            const bool http_ok = kernel_siec_pobierz_http(
                ip, "example.com", "/", odpowiedz, sizeof(odpowiedz));
            wypisz_log(http_ok ? "[NET-TEST] HTTP/80 example.com: OK" :
                                "[NET-TEST] HTTP/80 example.com: BLAD");
        }
    }
}
#endif

/* =========================================================================
 * POLACZENIA Z INNYMI MODULAMI
 * ========================================================================= */

extern "C" void InicjalizujGDT();
extern "C" void InicjalizujIDT();
extern "C" void inicjalizuj_apic();
extern "C" void InicjalizujMyszPS2();

extern "C" void inicjalizuj_tss(void* stos_jadra);
extern "C" void zaladuj_tss(uint16_t selektor_tss);
extern "C" void inicjalizuj_syscalls();
extern "C" void inicjalizuj_mbedtls();

/*
 * stack_top jest symbolem linkera z boot.S, a nie zmienna przechowujaca
 * adres. Deklaracja jako tablica pozwala uzyc samego adresu symbolu.
 */
extern "C" uint8_t stack_top[];

extern uint64_t najwyzsza_znaleziona_ramka;

extern "C" void skanuj_magistrale_pci();
extern "C" void wczytaj_tapete_z_dysku();

extern "C" bool usun_twor(const char* sciezka);
extern "C" bool utworz_plik(const char* sciezka);
extern "C" bool zapisz_do_pliku(const char* sciezka,
                                const char* dane,
                                uint32_t dlugosc);
extern "C" bool czytaj_z_pliku(const char* sciezka,
                                char* dane,
                                uint32_t dlugosc);
extern "C" bool utworz_katalog(const char* sciezka);

extern "C" void inicjalizuj_psf(void* adres_ram_dysku,
                                uint32_t rozmiar_w_bajtach);

extern "C" void uruchom_klienta_dhcp();

/* =========================================================================
 * WBUDOWANE PLIKI .BUR
 * ========================================================================= */

extern "C" uint8_t _binary_shell_bin_start[];
extern "C" uint8_t _binary_shell_bin_end[];
extern "C" uint8_t _binary_cytaty_txt_start[];
extern "C" uint8_t _binary_cytaty_txt_end[];

extern "C" uint8_t _binary_notatnik_bin_start[];
extern "C" uint8_t _binary_notatnik_bin_end[];

extern "C" uint8_t _binary_eksplorator_bin_start[];
extern "C" uint8_t _binary_eksplorator_bin_end[];

extern "C" uint8_t _binary_kalkulator_bin_start[];
extern "C" uint8_t _binary_kalkulator_bin_end[];

extern "C" uint8_t _binary_menedzer_okien_bin_start[];
extern "C" uint8_t _binary_menedzer_okien_bin_end[];

extern "C" uint8_t _binary_przegladarka_bin_start[];
extern "C" uint8_t _binary_przegladarka_bin_end[];

extern "C" uint8_t _binary_test_bin_start[];
extern "C" uint8_t _binary_test_bin_end[];

extern "C" uint8_t _binary_snake_bin_start[];
extern "C" uint8_t _binary_snake_bin_end[];

extern "C" uint8_t _binary_demo_ui_bin_start[];
extern "C" uint8_t _binary_demo_ui_bin_end[];

extern "C" uint8_t _binary_kontrolki2_bin_start[];
extern "C" uint8_t _binary_kontrolki2_bin_end[];

extern "C" uint8_t _binary_przegladarka_obrazow_bin_start[];
extern "C" uint8_t _binary_przegladarka_obrazow_bin_end[];

/* TEMP-REPRO: usunac po diagnozie BWS2/BWS46. */
extern "C" uint8_t _binary_repro_bin_start[];
extern "C" uint8_t _binary_repro_bin_end[];

// obraz testowy
extern "C" uint8_t _binary_obrazy_test_bmp_start[];
extern "C" uint8_t _binary_obrazy_test_bmp_end[];

extern "C" uint8_t _binary_obrazy_test1_bmp_start[];
extern "C" uint8_t _binary_obrazy_test1_bmp_end[];

/* =========================================================================
 * STALE STARTOWE
 * ========================================================================= */

namespace {

constexpr uint64_t MULTIBOOT2_BOOTLOADER_MAGIC = 0x36D76289ULL;

constexpr uint64_t ROZMIAR_STRONY = 4096ULL;
constexpr uint64_t FLAGI_STRONY_JADRA = 0b00000011ULL;

constexpr uint64_t ADRES_STERTY_JADRA = 0x500000000ULL;
constexpr uint64_t ROZMIAR_STERTY_JADRA =
    16ULL * 1024ULL * 1024ULL;

/*
 * Poprawiony grafika.cpp rezerwuje:
 *  0x100000000 - backbuffer
 *  0x110000000 - tapeta
 *  0x120000000 - surowy BMP
 *  0x130000000 - wirtualne mapowanie framebufferu
 *
 * Dlatego PSF nie moze juz uzywac 0x130000000.
 */
constexpr uint64_t ADRES_RAM_DYSKU_PSF = 0x200000000ULL;
constexpr uint32_t ROZMIAR_RAM_DYSKU_PSF =
    16U * 1024U * 1024U;

constexpr uint16_t PORT_COM1 = 0x3F8;

constexpr uint64_t PRAWA_MENEDZERA_OKIEN =
    PRAWO_GUI | PRAWO_URUCHOM_PROGRAM | PRAWO_SYSTEM_CONFIG |
    PRAWO_PLIKI_CZYTAJ | PRAWO_PLIKI_ZAPISZ | PRAWO_SIEC;

bool grafika_gotowa = false;
uint64_t g_multiboot_info_ptr = 0;

/* =========================================================================
 * NISKIE FUNKCJE STARTOWE
 * ========================================================================= */

static inline void serial_outb(uint16_t port,
                               uint8_t wartosc) {
    asm volatile(
        "outb %0, %1"
        :
        : "a"(wartosc), "Nd"(port)
        : "memory");
}

void serial_inicjalizuj() {
    serial_outb(PORT_COM1 + 1, 0x00);
    serial_outb(PORT_COM1 + 3, 0x80);
    serial_outb(PORT_COM1 + 0, 0x03);
    serial_outb(PORT_COM1 + 1, 0x00);
    serial_outb(PORT_COM1 + 3, 0x03);
    serial_outb(PORT_COM1 + 2, 0xC7);
    serial_outb(PORT_COM1 + 4, 0x0B);
}

void serial_wypisz(const char* tekst) {
    if (!tekst) return;

    for (size_t i = 0; tekst[i] != '\0'; ++i) {
        const uint8_t c = static_cast<uint8_t>(tekst[i]);
        serial_outb(PORT_COM1, c);
        serial_outb(0xE9, c);
    }
}

[[noreturn]]
void zatrzymaj_start(const char* powod) {
    asm volatile("cli" ::: "memory");

    serial_wypisz("\n[KERNEL-PANIC] ");
    serial_wypisz(
        powod ? powod : "Nieznany blad startu.");
    serial_wypisz("\n");

    if (grafika_gotowa) {
        wypisz_log("[KERNEL-PANIC] Start systemu zatrzymany.");
        if (powod)
            wypisz_log(powod);
    }

    while (true)
        asm volatile("hlt");
}

void przeladuj_cr3() {
    uint64_t cr3 = 0;

    asm volatile(
        "mov %%cr3, %0"
        : "=r"(cr3)
        :
        : "memory"
    );

    cr3 &= 0x000FFFFFFFFFF000ULL;

    if (cr3 == 0) {
        zatrzymaj_start(
            "Brak aktywnego PML4 podczas odswiezania mapowania."
        );
    }

    asm volatile(
        "mov %0, %%cr3"
        :
        : "r"(cr3)
        : "memory");
}

/* =========================================================================
 * BEZPIECZNE MAPOWANIE OBSZAROW JADRA
 * ========================================================================= */

bool zakres_stron_poprawny(uint64_t adres,
                           uint64_t rozmiar) {
    if (rozmiar == 0)
        return false;

    if ((adres & (ROZMIAR_STRONY - 1ULL)) != 0)
        return false;

    if ((rozmiar & (ROZMIAR_STRONY - 1ULL)) != 0)
        return false;

    if (adres > UINT64_MAX - rozmiar)
        return false;

    return true;
}

void mapuj_nowy_obszar_albo_zatrzymaj(
    uint64_t adres_wirtualny,
    uint64_t rozmiar,
    const char* komunikat_bledu) {

    if (!zakres_stron_poprawny(
            adres_wirtualny,
            rozmiar)) {
        zatrzymaj_start(
            "Niepoprawny zakres mapowania pamieci jadra.");
    }

    for (uint64_t offset = 0;
         offset < rozmiar;
         offset += ROZMIAR_STRONY) {

        void* ramka =
            ZaalokujRamke();

        /*
         * Nie wolno inicjalizowac sterty lub PSF na czesciowo
         * zmapowanym zakresie. Poprzednia wersja pomijala brakujace
         * ramki, a potem udostepniala caly obszar.
         */
        if (!ramka) {
            zatrzymaj_start(
                komunikat_bledu ?
                komunikat_bledu :
                "Brak fizycznej ramki pamieci.");
        }

        ZmapujStrone(
            reinterpret_cast<void*>(
                adres_wirtualny + offset),
            ramka,
            FLAGI_STRONY_JADRA);
    }

    przeladuj_cr3();
}

void wyzeruj_obszar(void* adres,
                    uint64_t rozmiar) {
    if (!adres || rozmiar == 0)
        return;

    volatile uint8_t* p =
        reinterpret_cast<volatile uint8_t*>(adres);

    for (uint64_t i = 0; i < rozmiar; ++i)
        p[i] = 0;
}

/* =========================================================================
 * PROSTE FUNKCJE TEKSTOWE
 * ========================================================================= */

bool uint_do_str(uint64_t wartosc,
                 char* bufor,
                 size_t pojemnosc) {
    if (!bufor || pojemnosc < 2)
        return false;

    char odwrotnie[32] = {};
    size_t n = 0;

    if (wartosc == 0) {
        bufor[0] = '0';
        bufor[1] = '\0';
        return true;
    }

    while (wartosc > 0) {
        if (n >= sizeof(odwrotnie))
            return false;

        odwrotnie[n++] =
            static_cast<char>(
                '0' + (wartosc % 10ULL));

        wartosc /= 10ULL;
    }

    if (n + 1 > pojemnosc)
        return false;

    size_t j = 0;

    while (n > 0)
        bufor[j++] = odwrotnie[--n];

    bufor[j] = '\0';
    return true;
}

bool dolacz_tekst(char* cel,
                  size_t pojemnosc,
                  const char* tekst) {
    if (!cel || !tekst || pojemnosc == 0)
        return false;

    size_t dst = 0;

    while (dst < pojemnosc &&
           cel[dst] != '\0') {
        ++dst;
    }

    if (dst >= pojemnosc)
        return false;

    size_t src = 0;

    while (tekst[src] != '\0') {
        if (dst + 1 >= pojemnosc)
            return false;

        cel[dst++] = tekst[src++];
    }

    cel[dst] = '\0';
    return true;
}

bool dlugosc_tekstu_u32(const char* tekst,
                        uint32_t* wynik) {
    if (!tekst || !wynik)
        return false;

    uint64_t dlugosc = 0;

    while (tekst[dlugosc] != '\0') {
        ++dlugosc;

        if (dlugosc > UINT32_MAX)
            return false;
    }

    *wynik =
        static_cast<uint32_t>(dlugosc);

    return true;
}

bool tekst_rowny(const char* a, const char* b) {
    if (!a || !b) return false;
    size_t i = 0;
    while (a[i] != '\0' && b[i] != '\0') {
        if (a[i] != b[i]) return false;
        ++i;
    }
    return a[i] == b[i];
}

bool tekst_zawiera(const char* caly, const char* fragment) {
    if (!caly || !fragment) return false;
    if (fragment[0] == '\0') return true;
    for (size_t i = 0; caly[i] != '\0'; ++i) {
        size_t j = 0;
        while (caly[i + j] != '\0' && fragment[j] != '\0' && caly[i + j] == fragment[j]) {
            ++j;
        }
        if (fragment[j] == '\0') return true;
    }
    return false;
}

/* =========================================================================
 * BEZPIECZNA INSTALACJA PLIKOW WBUDOWANYCH
 * ========================================================================= */

bool pobierz_rozmiar_bloba(
    const uint8_t* poczatek,
    const uint8_t* koniec,
    uint32_t* wynik) {

    if (!poczatek || !koniec || !wynik)
        return false;

    const uintptr_t start =
        reinterpret_cast<uintptr_t>(
            poczatek);

    const uintptr_t end =
        reinterpret_cast<uintptr_t>(
            koniec);

    if (end < start)
        return false;

    const uint64_t rozmiar =
        static_cast<uint64_t>(
            end - start);

    if (rozmiar == 0 ||
        rozmiar > UINT32_MAX) {
        return false;
    }

    *wynik =
        static_cast<uint32_t>(
            rozmiar);

    return true;
}

bool zapisz_plik_od_nowa(const char* sciezka,
                         const char* dane,
                         uint32_t rozmiar) {
    if (!sciezka)
        return false;

    if (rozmiar != 0 && !dane)
        return false;

    /*
     * usun_twor() moze zwrocic false, gdy plik jeszcze nie istnieje.
     * To nie jest blad instalacji.
     */
    (void)usun_twor(sciezka);

    if (!utworz_plik(sciezka))
        return false;

    if (rozmiar == 0)
        return true;

    if (!zapisz_do_pliku(
            sciezka,
            dane,
            rozmiar)) {

        /*
         * Nie zostawiamy polowicznie zainstalowanego pliku.
         */
        (void)usun_twor(sciezka);
        return false;
    }

    return true;
}

bool zapisz_blob_bur(
    const char* sciezka,
    const uint8_t* poczatek,
    const uint8_t* koniec) {

    uint32_t rozmiar = 0;

    if (!pobierz_rozmiar_bloba(
            poczatek,
            koniec,
            &rozmiar)) {
        return false;
    }

    /*
     * Minimalna kontrola chroni przed zapisaniem pustego/uszkodzonego
     * symbolu jako programu .bur.
     */
    if (rozmiar < sizeof(NaglowekBur))
        return false;

    return zapisz_plik_od_nowa(
        sciezka,
        reinterpret_cast<const char*>(poczatek),
        rozmiar);
}

bool zapisz_manifest(const char* sciezka,
                     const char* manifest) {
    uint32_t dlugosc = 0;

    if (!dlugosc_tekstu_u32(
            manifest,
            &dlugosc)) {
        return false;
    }

    return zapisz_plik_od_nowa(
        sciezka,
        manifest,
        dlugosc);
}

bool instaluj_paczke(
    const char* sciezka_manifestu,
    const char* manifest,
    const char* sciezka_programu,
    const uint8_t* blob_start,
    const uint8_t* blob_end) {

    if (!zapisz_manifest(
            sciezka_manifestu,
            manifest)) {
        return false;
    }

    if (!zapisz_blob_bur(
            sciezka_programu,
            blob_start,
            blob_end)) {
        (void)usun_twor(
            sciezka_manifestu);

        return false;
    }

    return true;
}

/* =========================================================================
 * SYSTEM PLIKOW
 * ========================================================================= */

/*
 * Sprawdza, czy podana sciezka wskazuje na istniejacy katalog.
 *
 * Uzywamy publicznego API PSF zamiast zagladac do jego prywatnych struktur.
 * wylistuj_katalog() zwraca true tylko dla poprawnego, istniejacego katalogu.
 * Bufor o rozmiarze 1 jest wystarczajacy do samego testu istnienia/typu.
 */
bool katalog_istnieje(const char* sciezka) {
    if (!sciezka)
        return false;

    char bufor_testowy[1] = {'\0'};

    return wylistuj_katalog(
        sciezka,
        bufor_testowy,
        sizeof(bufor_testowy));
}




/*
 * Zapewnia istnienie katalogu, ale nie traktuje ponownego uruchomienia
 * systemu jako bledu.
 *
 * To jest wazne dla trwalego BSP2: po pierwszym uruchomieniu katalogi sa
 * juz zapisane na SATA. utworz_katalog() zgodnie z API zwraca false, gdy
 * obiekt juz istnieje, dlatego nie wolno uzywac samego wyniku CREATE jako
 * testu poprawnosci startu.
 */
bool zapewnij_katalog(const char* sciezka) {
    if (!sciezka)
        return false;

    if (katalog_istnieje(sciezka))
        return true;

    if (!utworz_katalog(sciezka))
        return false;

    /*
     * Weryfikujemy rezultat po utworzeniu. Chroni to start przed sytuacja,
     * w ktorej implementacja PSF zwroci sukces, ale katalog nie bedzie
     * mozliwy do poprawnego odczytania.
     */
    return katalog_istnieje(sciezka);
}

void utworz_drabine_katalogow() {
    static const char* const KATALOGI[] = {
        "/jadro",
        "/system",
        "/programy",
        "/programy/notatnik.cebula",
        "/programy/eksplorator.cebula",
        "/programy/kalkulator.cebula",
        "/programy/przegladarka.cebula",
        "/programy/przegladarka-obrazow.cebula",
        "/programy/demo_ui.cebula",
        "/programy/kontrolki2.cebula",
        "/programy/test.cebula",
        "/gry",
        "/gry/snake.cebula",
        "/uslugi",
        "/sterowniki",
        "/uzytkownicy",
        "/uzytkownicy/Pulpit",
        "/ustawienia",
        "/logi",
        "/piaskownica",
        "/tymczasowe"
    };

    for (size_t i = 0;
         i < sizeof(KATALOGI) / sizeof(KATALOGI[0]);
         ++i) {

        if (!zapewnij_katalog(KATALOGI[i])) {
            /*
             * Dopuszczamy katalogi odziedziczone z trwalego BSP2, ale nadal
             * zatrzymujemy start, jezeli wymaganej sciezki nie mozna ani
             * odczytac jako katalogu, ani poprawnie utworzyc.
             */
            zatrzymaj_start(
                "Nie mozna przygotowac podstawowego drzewa katalogow PSF.");
        }
    }
}

/* =========================================================================
 * IMPORT MODULOW MULTIBOOT2 (test8k.bmp)
 * ========================================================================= */

struct ModulMB2 {
    uint64_t start;
    uint64_t koniec;
    uint64_t rozmiar;
    const char* cmdline;
    bool poprawny;
};

bool znajdz_modul_mb2(uint64_t mbi, const char* nazwa_szukana, ModulMB2* wynik) {
    if (!wynik || mbi == 0 || !nazwa_szukana) return false;
    wynik->poprawny = false;

    const volatile uint32_t* info = reinterpret_cast<const volatile uint32_t*>(mbi);
    const uint32_t calosc = info[0];
    if (calosc < 16 || mbi > UINT64_MAX - calosc) return false;

    uint64_t off = 8;
    while (off + sizeof(WpisTaguMB2) <= calosc) {
        const WpisTaguMB2* tag = reinterpret_cast<const WpisTaguMB2*>(mbi + off);
        if (tag->rozmiar < sizeof(WpisTaguMB2) || off + tag->rozmiar > calosc) return false;

        if (tag->typ == 3 /* MULTIBOOT_TAG_TYPE_MODULE */ && tag->rozmiar >= 16) {
            const uint32_t mod_start = *reinterpret_cast<const uint32_t*>(mbi + off + 8);
            const uint32_t mod_end   = *reinterpret_cast<const uint32_t*>(mbi + off + 12);
            const char* cmdline = reinterpret_cast<const char*>(mbi + off + 16);
            const uint32_t max_cmdline_len = tag->rozmiar - 16;

            bool ma_nul = false;
            for (uint32_t i = 0; i < max_cmdline_len; ++i) {
                if (cmdline[i] == '\0') {
                    ma_nul = true;
                    break;
                }
            }

            if (ma_nul && mod_end > mod_start) {
                if (tekst_rowny(cmdline, nazwa_szukana) || tekst_zawiera(cmdline, nazwa_szukana)) {
                    wynik->start = static_cast<uint64_t>(mod_start);
                    wynik->koniec = static_cast<uint64_t>(mod_end);
                    wynik->rozmiar = static_cast<uint64_t>(mod_end - mod_start);
                    wynik->cmdline = cmdline;
                    wynik->poprawny = true;
                    return true;
                }
            }
        }

        if (tag->typ == MULTIBOOT_TAG_TYPE_END) break;
        off = (off + tag->rozmiar + 7ULL) & ~7ULL;
    }

    return false;
}

void importuj_modul_test8k(uint64_t mbi) {
    if (mbi == 0) return;

    ModulMB2 mod{};
    if (!znajdz_modul_mb2(mbi, "test8k.bmp", &mod) || !mod.poprawny) {
        return;
    }

    const uint64_t start = mod.start;
    const uint64_t rozmiar = mod.rozmiar;
    const char* sciezka_docelowa = "/uzytkownicy/Pulpit/test8k.bmp";

    wypisz_log("[BSP] Znaleziono modul Multiboot2: test8k.bmp");

    if (start == 0 || start >= 0x100000000ULL || mod.koniec > 0x100000000ULL ||
        rozmiar == 0 || rozmiar > (256ULL * 1024ULL * 1024ULL)) {
        wypisz_log("[BSP-BLAD] Nieprawidlowy zakres lub rozmiar modulu test8k.bmp.");
        OdblokujZakres(start, rozmiar);
        return;
    }

    const uint8_t* module_data = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(start));

    if (module_data[0] != 0x42 || module_data[1] != 0x4D) {
        wypisz_log("[BSP-BLAD] Modul test8k.bmp nie posiada sygnatury BMP (0x42, 0x4D).");
        OdblokujZakres(start, rozmiar);
        return;
    }

    const uint32_t istniejacy_rozmiar = rozmiar_pliku(sciezka_docelowa);
    if (istniejacy_rozmiar == static_cast<uint32_t>(rozmiar)) {
        char hdr[2] = {0, 0};
        uint32_t odczytano = 0;
        if (czytaj_fragment_pliku(sciezka_docelowa, 0, hdr, 2, &odczytano) &&
            odczytano == 2 &&
            static_cast<uint8_t>(hdr[0]) == 0x42 &&
            static_cast<uint8_t>(hdr[1]) == 0x4D) {
            wypisz_log("[BSP] test8k.bmp juz istnieje - pomijam import.");
            OdblokujZakres(start, rozmiar);
            return;
        }
    }

    (void)usun_twor(sciezka_docelowa);
    if (!utworz_plik(sciezka_docelowa)) {
        wypisz_log("[BSP-BLAD] Nie mozna utworzyc pliku /uzytkownicy/Pulpit/test8k.bmp w PSF.");
        OdblokujZakres(start, rozmiar);
        return;
    }

    wypisz_log("[BSP] Rozpoczynam strumieniowy import test8k.bmp do PSF...");

    constexpr uint64_t CHUNK_SIZE = 256ULL * 1024ULL; // 256 KiB
    uint64_t offset = 0;
    bool zapis_sukces = true;

    while (offset < rozmiar) {
        const uint64_t pozostalo = rozmiar - offset;
        const uint32_t chunk = (pozostalo > CHUNK_SIZE) ? static_cast<uint32_t>(CHUNK_SIZE) : static_cast<uint32_t>(pozostalo);
        const char* chunk_ptr = reinterpret_cast<const char*>(module_data + offset);

        if (!zapisz_fragment_pliku(sciezka_docelowa, offset, chunk_ptr, chunk)) {
            zapis_sukces = false;
            break;
        }
        offset += chunk;
    }

    if (!zapis_sukces) {
        wypisz_log("[BSP-BLAD] Blad podczas strumieniowego zapisu test8k.bmp.");
        (void)usun_twor(sciezka_docelowa);
        OdblokujZakres(start, rozmiar);
        return;
    }

    (void)psf_flush_cache();

    const uint32_t psf_sz = rozmiar_pliku(sciezka_docelowa);
    if (psf_sz != static_cast<uint32_t>(rozmiar)) {
        wypisz_log("[BSP-BLAD] Mismatch rozmiaru test8k.bmp w PSF po zapisie.");
        (void)usun_twor(sciezka_docelowa);
        OdblokujZakres(start, rozmiar);
        return;
    }

    char hdr_weryfikacja[2] = {0, 0};
    uint32_t odczytano_weryfikacja = 0;
    if (!czytaj_fragment_pliku(sciezka_docelowa, 0, hdr_weryfikacja, 2, &odczytano_weryfikacja) ||
        odczytano_weryfikacja != 2 ||
        static_cast<uint8_t>(hdr_weryfikacja[0]) != 0x42 ||
        static_cast<uint8_t>(hdr_weryfikacja[1]) != 0x4D) {
        wypisz_log("[BSP-BLAD] Blad weryfikacji naglowka BM w PSF dla test8k.bmp.");
        (void)usun_twor(sciezka_docelowa);
        OdblokujZakres(start, rozmiar);
        return;
    }

    wypisz_log("[BSP] test8k.bmp zaimportowany pomyslnie do PSF.");

    OdblokujZakres(start, rozmiar);
    wypisz_log("[PMM] Pamiec fizyczna modulu test8k.bmp zwolniona do puli PMM.");
}

void instaluj_wbudowane_programy() {
    static const char MANIFEST_PRZEGLADARKI[] =
        "nazwa = \"Husarz\"\n"
        "autor = \"Programista Art\"\n"
        "wersja = \"0.1\"\n"
        "poziom_zaufania = 4\n"
        "plik_startowy = \"przegladarka.bur\"\n"
        "uprawnienia = [\n"
        "    \"okna\",\n"
        "    \"siec\",\n"
        "    \"pliki_czytaj\"\n"
        "]\n";

    static const char MANIFEST_NOTATNIKA[] =
        "nazwa = \"Notatnik\"\n"
        "autor = \"Programista Art\"\n"
        "wersja = \"0.1\"\n"
        "poziom_zaufania = 4\n"
        "plik_startowy = \"notatnik.bur\"\n"
        "uprawnienia = [\n"
        "    \"okna\",\n"
        "    \"pliki_czytaj\",\n"
        "    \"pliki_zapisz\"\n"
        "]\n";

    static const char MANIFEST_EKSPLORATORA[] =
        "nazwa = \"Eksplorator Plików\"\n"
        "autor = \"Programista Art\"\n"
        "wersja = \"1.0\"\n"
        "poziom_zaufania = 4\n"
        "plik_startowy = \"eksplorator-plikow.bur\"\n"
        "uprawnienia = [\n"
        "    \"okna\",\n"
        "    \"pliki_czytaj\",\n"
        "    \"pliki_zapisz\",\n"
        "    \"uruchom_program\"\n"
        "]\n";

    static const char MANIFEST_KALKULATORA[] =
        "nazwa = \"Kalkulator\"\n"
        "autor = \"Programista Art\"\n"
        "wersja = \"1.0\"\n"
        "poziom_zaufania = 4\n"
        "plik_startowy = \"kalkulator.bur\"\n"
        "uprawnienia = [\n"
        "    \"okna\"\n"
        "]\n";

    static const char MANIFEST_TEST[] =
        "nazwa = \"Test\"\n"
        "autor = \"Programista Art\"\n"
        "wersja = \"1.0\"\n"
        "poziom_zaufania = 4\n"
        "plik_startowy = \"test.bur\"\n"
        "uprawnienia = [\n"
        "    \"okna\"\n"
        "]\n";
    
    static const char MANIFEST_SNAKE[] =
        "nazwa = \"Snake\"\n"
        "autor = \"Programista Art\"\n"
        "wersja = \"1.0\"\n"
        "poziom_zaufania = 4\n"
        "plik_startowy = \"snake.bur\"\n"
        "uprawnienia = [\n"
        "    \"okna\"\n"
        "]\n";

    static const char MANIFEST_PRZEGLADARKA_OBRAZOW[] =
        "nazwa = \"Przeglądarka Obrazów\"\n"
        "autor = \"Programista Art\"\n"
        "wersja = \"0.1\"\n"
        "poziom_zaufania = 4\n"
        "plik_startowy = \"przegladarka-obrazow.bur\"\n"
        "uprawnienia = [\n"
        "    \"okna\",\n"
        "    \"pliki_czytaj\"\n"
        "]\n";

    static const char MANIFEST_DEMO_UI[] =
        "nazwa = \"BursztynUI Demo\"\n"
        "autor = \"Programista Art\"\n"
        "wersja = \"0.1\"\n"
        "poziom_zaufania = 4\n"
        "plik_startowy = \"demo_ui.bur\"\n"
        "uprawnienia = [\n"
        "    \"okna\"\n"
        "]\n";

    static const char MANIFEST_KONTROLKI2[] =
        "nazwa = \"Kontrolki 2\"\n"
        "autor = \"Programista Art\"\n"
        "wersja = \"0.1\"\n"
        "poziom_zaufania = 4\n"
        "plik_startowy = \"kontrolki2.bur\"\n"
        "uprawnienia = [\n"
        "    \"okna\"\n"
        "]\n";
    
    /*
     * WAZNE:
     * zapisujemy DOKLADNY rozmiar kazdego bloba z objcopy.
     *
     * Poprzedni kod wymuszal minimum 24576 bajtow dla kilku aplikacji.
     * Gdy blob byl mniejszy, zapisz_do_pliku() czytal wtedy pamiec ZA
     * symbolem _binary_*_end. To moglo kopiowac przypadkowe dane jadra.
     */
    if (!zapisz_blob_bur(
            "/shell.bur",
            _binary_shell_bin_start,
            _binary_shell_bin_end)) {

        zatrzymaj_start(
            "Nie mozna zainstalowac /shell.bur.");
    }

    wypisz_log(
        "[BSP] Wbudowana Powloka zainstalowana.");

    uint32_t cytaty_rozmiar = 0;
    if (!pobierz_rozmiar_bloba(_binary_cytaty_txt_start,
                               _binary_cytaty_txt_end, &cytaty_rozmiar) ||
        !zapisz_plik_od_nowa("/cytaty.txt",
            reinterpret_cast<const char*>(_binary_cytaty_txt_start),
            cytaty_rozmiar)) {
        wypisz_log("[BSP-BLAD] Nie mozna zainstalowac /cytaty.txt.");
    } else {
        wypisz_log("[BSP] /cytaty.txt zainstalowany.");
    }

    //testowy obrazek test.bmp + test1.bmp - diagnostyka i atomowość
#if BURSZTYN_TESTY_PSF
    auto log_u32 = [](const char* pref, uint32_t v){
        char buf[96]={0};
        size_t p=0;
        while(pref[p] && p<80){buf[p]=pref[p];p++;}
        // dec
        char tmp[12]={0};
        int n=0;
        if(v==0) tmp[n++]='0';
        else {char rev[12];int r=0;uint32_t t=v;while(t){rev[r++]='0'+t%10;t/=10;}while(r--) tmp[n++]=rev[r];}
        tmp[n]='\0';
        size_t i=0;
        while(tmp[i] && p<95){buf[p++]=tmp[i++];}
        buf[p]='\0';
        wypisz_log(buf);
    };
#endif
    const uint64_t test_bmp_rozmiar =
        static_cast<uint64_t>(
            _binary_obrazy_test_bmp_end -
            _binary_obrazy_test_bmp_start);
    const uint64_t test1_bmp_rozmiar =
        static_cast<uint64_t>(
            _binary_obrazy_test1_bmp_end -
            _binary_obrazy_test1_bmp_start);
#if BURSZTYN_TESTY_PSF
    log_u32("[BSP-TEST] test.bmp blob size = ", (uint32_t)test_bmp_rozmiar);
    log_u32("[BSP-TEST1] test1.bmp blob size = ", (uint32_t)test1_bmp_rozmiar);
    if(test_bmp_rozmiar != test1_bmp_rozmiar){
        wypisz_log("[BSP-TEST] UWAGA: rozmiary blob roznia sie");
    }
#endif
    // helper lambda do instalacji z diagnostyka i read-back
    auto instaluj_bmp = [&](const char* sciezka, const uint8_t* blob, uint32_t sz, const char* tag){
#if BURSZTYN_TESTY_PSF
        // usun
        bool usun = usun_twor(sciezka);
        (void)usun;
        // utworz
        bool ut = utworz_plik(sciezka);
        {
            char b[96]={0};
            const char* pre="[BSP-TEST] utworz_plik ";
            size_t p=0; while(pre[p]){b[p]=pre[p];p++;} size_t i=0; while(sciezka[i] && p<80){b[p++]=sciezka[i++];} b[p++]='='; b[p++]=' '; const char* s=ut?"OK":"FAIL"; size_t j=0; while(s[j]&&p<95) b[p++]=s[j++]; b[p]='\0'; wypisz_log(b);
        }
        if(!ut){
            wypisz_log("[BSP-TEST] utworz_plik FAIL - przerywam");
            return;
        }
        bool zap = zapisz_do_pliku(sciezka, reinterpret_cast<const char*>(blob), sz);
        {
            char b[96]={0};
            const char* pre="[BSP-TEST] zapisz_do_pliku ";
            size_t p=0; while(pre[p]){b[p]=pre[p];p++;} size_t i=0; while(sciezka[i] && p<70){b[p++]=sciezka[i++];} b[p++]='='; b[p++]=' '; const char* s=zap?"OK":"FAIL"; size_t j=0; while(s[j]&&p<95) b[p++]=s[j++]; b[p]='\0'; wypisz_log(b);
        }
        if(!zap){
            wypisz_log("[BSP-TEST] zapisz FAIL -> usuwam niekompletny plik");
            (void)usun_twor(sciezka);
            return;
        }
        wypisz_log("[BSP-TEST] instalacja OK");
        // read-back
        uint32_t psf_sz = 0;
        psf_sz = rozmiar_pliku(sciezka);
        log_u32("[BSP-TEST] rozmiar w PSF = ", psf_sz);
        if(psf_sz != sz){
            wypisz_log("[BSP-TEST] mismatch rozmiar PSF vs blob");
        } else {
            wypisz_log("[BSP-TEST] rozmiar PSF OK");
        }
        if(psf_sz>0){
            char hdr[64]={0};
            bool rh = czytaj_z_pliku(sciezka, hdr, 64);
            bool ok=true;
            if(rh){
                for(int i=0;i<64 && i<(int)sz;i++) if((uint8_t)hdr[i] != blob[i]){ok=false;break;}
            } else ok=false;
            wypisz_log(ok?"[BSP-TEST] pierwsze 64B OK":"[BSP-TEST] mismatch pierwsze 64B");
            if(rh && (uint8_t)hdr[0]==0x42 && (uint8_t)hdr[1]==0x4D) wypisz_log("[BSP-TEST] BM OK");
            else wypisz_log("[BSP-TEST] BM FAIL");
            // pełne byte-for-byte
            if(psf_sz==sz && rh && ok){
                bool full_ok=true;
                uint32_t mismatch_off=0;
                if(sz <= 1024*1024){
                    char* all = static_cast<char*>(kmalloc(sz));
                    if(all && czytaj_z_pliku(sciezka, all, sz)){
                        for(uint32_t i=0;i<sz;i++) if((uint8_t)all[i]!=blob[i]){full_ok=false; mismatch_off=i; break;}
                    } else full_ok=false;
                    if(all) kfree(all);
                } else {
                    full_ok=true;
                }
                if(full_ok) wypisz_log("[BSP-TEST] byte-for-byte PASS");
                else {
                    char m[64]={0};
                    const char* pre="[BSP-TEST] mismatch offset=";
                    size_t p=0; while(pre[p]){m[p]=pre[p];p++;}
                    char tmp2[12]={0}; int n=0; uint32_t v=mismatch_off;
                    if(v==0) tmp2[n++]='0';
                    else {char rev[12];int r=0;uint32_t t=v;while(t){rev[r++]='0'+t%10;t/=10;}while(r--) tmp2[n++]=rev[r];}
                    size_t i=0; while(tmp2[i]&&p<60) m[p++]=tmp2[i++];
                    m[p]='\0';
                    wypisz_log(m);
                }
            }
        }
#else
        if (rozmiar_pliku(sciezka) == sz) {
            return;
        }
        (void)usun_twor(sciezka);
        if (!utworz_plik(sciezka)) return;
        (void)zapisz_do_pliku(sciezka, reinterpret_cast<const char*>(blob), sz);
#endif
        (void)tag;
    };
    instaluj_bmp("/uzytkownicy/Pulpit/test.bmp", _binary_obrazy_test_bmp_start, (uint32_t)test_bmp_rozmiar, "test");
    instaluj_bmp("/uzytkownicy/Pulpit/test1.bmp", _binary_obrazy_test1_bmp_start, (uint32_t)test1_bmp_rozmiar, "test1");

#if BURSZTYN_TESTY_PSF
    auto generuj_bmp_strumieniowo = [](const char* sciezka, uint32_t w, uint32_t h, uint16_t bpp) -> bool {
        const uint32_t stride = ((w * bpp + 31U) / 32U) * 4U;
        const uint64_t image_data_size = static_cast<uint64_t>(stride) * static_cast<uint64_t>(h);
        const uint64_t total_size = 54ULL + image_data_size;

        const uint32_t rsz = rozmiar_pliku(sciezka);
        if (static_cast<uint64_t>(rsz) == total_size) {
            char logbuf[96] = "[BMP-GEN] Istnieje: ";
            size_t p = 0; while (logbuf[p]) ++p;
            size_t s = 0; while (sciezka[s] && p + 1 < sizeof(logbuf)) logbuf[p++] = sciezka[s++];
            logbuf[p] = '\0';
            wypisz_log(logbuf);
            return true;
        }

        (void)usun_twor(sciezka);
        if (!utworz_plik(sciezka)) {
            wypisz_log("[BMP-GEN] Nie mozna utworzyc pliku BMP.");
            return false;
        }

        const uint32_t CHUNK = 256 * 1024;
        uint8_t* bufor = static_cast<uint8_t*>(kmalloc(CHUNK));
        if (!bufor) {
            wypisz_log("[BMP-GEN] Brak pamieci na bufor.");
            return false;
        }

        uint8_t hdr[54] = {
            'B', 'M',
            static_cast<uint8_t>(total_size & 0xFF),
            static_cast<uint8_t>((total_size >> 8) & 0xFF),
            static_cast<uint8_t>((total_size >> 16) & 0xFF),
            static_cast<uint8_t>((total_size >> 24) & 0xFF),
            0, 0, 0, 0,
            54, 0, 0, 0,
            40, 0, 0, 0,
            static_cast<uint8_t>(w & 0xFF),
            static_cast<uint8_t>((w >> 8) & 0xFF),
            static_cast<uint8_t>((w >> 16) & 0xFF),
            static_cast<uint8_t>((w >> 24) & 0xFF),
            static_cast<uint8_t>(h & 0xFF),
            static_cast<uint8_t>((h >> 8) & 0xFF),
            static_cast<uint8_t>((h >> 16) & 0xFF),
            static_cast<uint8_t>((h >> 24) & 0xFF),
            1, 0,
            static_cast<uint8_t>(bpp & 0xFF),
            static_cast<uint8_t>((bpp >> 8) & 0xFF),
            0, 0, 0, 0,
            static_cast<uint8_t>(image_data_size & 0xFF),
            static_cast<uint8_t>((image_data_size >> 8) & 0xFF),
            static_cast<uint8_t>((image_data_size >> 16) & 0xFF),
            static_cast<uint8_t>((image_data_size >> 24) & 0xFF),
            0x13, 0x0B, 0, 0,
            0x13, 0x0B, 0, 0,
            0, 0, 0, 0,
            0, 0, 0, 0
        };

        // Umiesc naglowek 54B na poczatku pierwszego bufora, aby zapisy byly wyrownane
        for (size_t i = 0; i < 54; ++i) bufor[i] = hdr[i];
        uint32_t buf_pos = 54;
        uint64_t file_offset = 0;
        const uint32_t row_bytes = (bpp == 24) ? (w * 3) : (w * 4);
        const uint32_t pad = stride - row_bytes;

        for (uint32_t y = 0; y < h; ++y) {
            const uint8_t g = static_cast<uint8_t>(y & 0xFF);
            for (uint32_t x = 0; x < w; ++x) {
                const uint8_t r = static_cast<uint8_t>(x & 0xFF);
                const uint8_t b = static_cast<uint8_t>((x ^ y) & 0xFF);

                if (bpp == 24) {
                    bufor[buf_pos++] = b;
                    bufor[buf_pos++] = g;
                    bufor[buf_pos++] = r;
                } else {
                    *reinterpret_cast<uint32_t*>(bufor + buf_pos) =
                        0xFF000000U | (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(b);
                    buf_pos += 4;
                }

                if (buf_pos + 4 >= CHUNK) {
                    if (!zapisz_fragment_pliku(sciezka, file_offset, reinterpret_cast<const char*>(bufor), buf_pos)) {
                        wypisz_log("[BMP-GEN] Blad zapisu pikseli.");
                        kfree(bufor);
                        return false;
                    }
                    file_offset += buf_pos;
                    buf_pos = 0;
                }
            }

            for (uint32_t p = 0; p < pad; ++p) {
                bufor[buf_pos++] = 0;
                if (buf_pos >= CHUNK) {
                    if (!zapisz_fragment_pliku(sciezka, file_offset, reinterpret_cast<const char*>(bufor), buf_pos)) {
                        wypisz_log("[BMP-GEN] Blad zapisu paddingu.");
                        kfree(bufor);
                        return false;
                    }
                    file_offset += buf_pos;
                    buf_pos = 0;
                }
            }
        }

        if (buf_pos > 0) {
            if (!zapisz_fragment_pliku(sciezka, file_offset, reinterpret_cast<const char*>(bufor), buf_pos)) {
                wypisz_log("[BMP-GEN] Blad zapisu koncowki pikseli.");
                kfree(bufor);
                return false;
            }
            file_offset += buf_pos;
        }

        kfree(bufor);
        (void)psf_flush_cache();

        char logok[96] = "[BMP-GEN] Utworzono: ";
        size_t p = 0; while (logok[p]) ++p;
        size_t s = 0; while (sciezka[s] && p + 1 < sizeof(logok)) logok[p++] = sciezka[s++];
        logok[p] = '\0';
        wypisz_log(logok);
        return true;
    };

    generuj_bmp_strumieniowo("/uzytkownicy/Pulpit/obraz_4k_24b.bmp", 3840, 2160, 24);
    generuj_bmp_strumieniowo("/uzytkownicy/Pulpit/obraz_8k_24b.bmp", 7680, 4320, 24);
    generuj_bmp_strumieniowo("/uzytkownicy/Pulpit/obraz_8k_32b.bmp", 7680, 4320, 32);
#endif


    if (!instaluj_paczke(
            "/programy/przegladarka.cebula/opis.aplikacji",
            MANIFEST_PRZEGLADARKI,
            "/programy/przegladarka.cebula/przegladarka.bur",
            _binary_przegladarka_bin_start,
            _binary_przegladarka_bin_end)) {

        wypisz_log(
            "[BSP-BLAD] Nie mozna zainstalowac przegladarki Husarz.");
    } else {
        wypisz_log(
            "[BSP] Przegladarka Husarz zainstalowana jako .cebula.");
    }

    if (!instaluj_paczke(
            "/programy/notatnik.cebula/opis.aplikacji",
            MANIFEST_NOTATNIKA,
            "/programy/notatnik.cebula/notatnik.bur",
            _binary_notatnik_bin_start,
            _binary_notatnik_bin_end)) {

        wypisz_log(
            "[BSP-BLAD] Nie mozna zainstalowac Notatnika.");
    } else {
        wypisz_log(
            "[BSP] Notatnik zainstalowany jako .cebula.");
    }

    if (!instaluj_paczke(
            "/programy/eksplorator.cebula/opis.aplikacji",
            MANIFEST_EKSPLORATORA,
            "/programy/eksplorator.cebula/eksplorator-plikow.bur",
            _binary_eksplorator_bin_start,
            _binary_eksplorator_bin_end)) {

        wypisz_log(
            "[BSP-BLAD] Nie mozna zainstalowac Eksploratora Plikow.");
    } else {
        wypisz_log(
            "[BSP] Eksplorator Plikow zainstalowany jako .cebula.");
    }

    //Program testowy
    if (!instaluj_paczke(
            "/programy/test.cebula/opis.aplikacji",
             MANIFEST_TEST,
            "/programy/test.cebula/test.bur",
            _binary_test_bin_start,
            _binary_test_bin_end)) {

        wypisz_log(
            "[BSP-BLAD] Nie mozna zainstalowac Aplikacji Testowej.");
    } else {
        wypisz_log(
            "[BSP] Aplikacja Testowa zainstalowana jako .cebula.");
    }
    //Gra snake
        if (!instaluj_paczke(
            "/gry/snake.cebula/opis.aplikacji",
             MANIFEST_SNAKE,
            "/gry/snake.cebula/snake.bur",
            _binary_snake_bin_start,
            _binary_snake_bin_end)) {

        wypisz_log(
            "[BSP-BLAD] Nie mozna zainstalowac gry Snake.");
    } else {
        wypisz_log(
            "[BSP] Gra snake zainstalowana jako .cebula.");
    }

    if (!instaluj_paczke(
            "/programy/przegladarka-obrazow.cebula/opis.aplikacji",
            MANIFEST_PRZEGLADARKA_OBRAZOW,
            "/programy/przegladarka-obrazow.cebula/przegladarka-obrazow.bur",
            _binary_przegladarka_obrazow_bin_start,
            _binary_przegladarka_obrazow_bin_end)) {
        wypisz_log(
            "[BSP-BLAD] Nie mozna zainstalowac Przegladarki Obrazow.");
    } else {
        wypisz_log(
            "[BSP] Przegladarka Obrazow zainstalowana jako .cebula.");
    }

    if (!instaluj_paczke(
            "/programy/demo_ui.cebula/opis.aplikacji",
            MANIFEST_DEMO_UI,
            "/programy/demo_ui.cebula/demo_ui.bur",
            _binary_demo_ui_bin_start,
            _binary_demo_ui_bin_end)) {
        wypisz_log(
            "[BSP-BLAD] Nie mozna zainstalowac BursztynUI Demo.");
    } else {
        wypisz_log(
            "[BSP] BursztynUI Demo zainstalowane jako .cebula.");
    }

    if (!instaluj_paczke(
            "/programy/kontrolki2.cebula/opis.aplikacji",
            MANIFEST_KONTROLKI2,
            "/programy/kontrolki2.cebula/kontrolki2.bur",
            _binary_kontrolki2_bin_start,
            _binary_kontrolki2_bin_end)) {
        wypisz_log(
            "[BSP-BLAD] Nie mozna zainstalowac Kontrolek 2.");
    } else {
        wypisz_log(
            "[BSP] Kontrolki 2 zainstalowane jako .cebula.");
    }


/* TEMP-REPRO instalacja wylaczona na czas testu GUI */

    if (!instaluj_paczke(
            "/programy/kalkulator.cebula/opis.aplikacji",
            MANIFEST_KALKULATORA,
            "/programy/kalkulator.cebula/kalkulator.bur",
            _binary_kalkulator_bin_start,
            _binary_kalkulator_bin_end)) {

        wypisz_log(
            "[BSP-BLAD] Nie mozna zainstalowac Kalkulatora.");
    } else {
        wypisz_log(
            "[BSP] Kalkulator zainstalowany jako .cebula.");
    }

    if (!zapisz_blob_bur(
            "/menedzer_okien.bur",
            _binary_menedzer_okien_bin_start,
            _binary_menedzer_okien_bin_end)) {

        zatrzymaj_start(
            "Nie mozna zainstalowac Menedzera Okien.");
    }

    wypisz_log(
        "[BSP] Menedzer Okien zainstalowany.");
#if BURSZTYN_TESTY_PSF
    // Test granic indirect oraz duzych plikow PSF – strumieniowo
    {
        auto fn_wzorzec64 = [](uint64_t off) -> uint64_t {
            return (off * 6364136223846793005ULL) ^ 0xA5A5A5A5A5A5A5A5ULL;
        };

        auto test_plik_strumieniowy = [&](uint64_t sz, const char* name, const char* prefix_log) {
            char sciezka[64] = {0};
            const char* pref = "/logi/";
            size_t i = 0; while (pref[i] && i < 30) { sciezka[i] = pref[i]; i++; }
            size_t j = 0; while (name[j] && i < 60) { sciezka[i++] = name[j++]; }
            sciezka[i] = '\0';

            const uint32_t CHUNK = 256 * 1024; // 256 KiB bufor testowy
            uint8_t* bufor = static_cast<uint8_t*>(kmalloc(CHUNK));
            if (!bufor) {
                wypisz_log("[PSF-TEST] kmalloc fail");
                return;
            }

            uint32_t istniejacy_rsz = rozmiar_pliku(sciezka);
            bool juz_istnieje = (istniejacy_rsz == sz);

            if (!juz_istnieje) {
                (void)usun_twor(sciezka);
                if (!utworz_plik(sciezka)) {
                    wypisz_log("[PSF-TEST] utworz FAIL");
                    kfree(bufor);
                    return;
                }

                // Zapis strumieniowy deterministycznym wzorcem
                for (uint64_t off = 0; off < sz; off += CHUNK) {
                    uint32_t por = (sz - off > CHUNK) ? CHUNK : static_cast<uint32_t>(sz - off);
                    uint64_t* b64 = reinterpret_cast<uint64_t*>(bufor);
                    uint32_t words = por / sizeof(uint64_t);
                    for (uint32_t w = 0; w < words; ++w) {
                        b64[w] = fn_wzorzec64(off + w * sizeof(uint64_t));
                    }
                    uint32_t tail_start = words * sizeof(uint64_t);
                    if (tail_start < por) {
                        uint64_t tail_val = fn_wzorzec64(off + tail_start);
                        for (uint32_t k = tail_start; k < por; ++k) {
                            bufor[k] = reinterpret_cast<uint8_t*>(&tail_val)[k - tail_start];
                        }
                    }
                    if (!zapisz_fragment_pliku(sciezka, off, reinterpret_cast<const char*>(bufor), por)) {
                        wypisz_log("[PSF-TEST] zapisz_fragment FAIL");
                        kfree(bufor);
                        return;
                    }
                }

                // Flush cache do AHCI
                if (!psf_flush_cache()) {
                    wypisz_log("[PSF-TEST] flush FAIL");
                }
            }

            // Odczyt strumieniowy i weryfikacja byte-for-byte
            bool ok = true;
            for (uint64_t off = 0; off < sz; off += CHUNK) {
                uint32_t por = (sz - off > CHUNK) ? CHUNK : static_cast<uint32_t>(sz - off);
                uint32_t got = 0;
                if (!czytaj_fragment_pliku(sciezka, off, reinterpret_cast<char*>(bufor), por, &got) || got != por) {
                    ok = false;
                    break;
                }
                uint64_t* b64 = reinterpret_cast<uint64_t*>(bufor);
                uint32_t words = por / sizeof(uint64_t);
                for (uint32_t w = 0; w < words; ++w) {
                    if (b64[w] != fn_wzorzec64(off + w * sizeof(uint64_t))) {
                        ok = false;
                        break;
                    }
                }
                if (!ok) break;
                uint32_t tail_start = words * sizeof(uint64_t);
                if (tail_start < por) {
                    uint64_t tail_val = fn_wzorzec64(off + tail_start);
                    for (uint32_t k = tail_start; k < por; ++k) {
                        if (bufor[k] != reinterpret_cast<uint8_t*>(&tail_val)[k - tail_start]) {
                            ok = false;
                            break;
                        }
                    }
                }
                if (!ok) break;
            }

            kfree(bufor);

            if (!ok) {
                wypisz_log("[PSF-TEST] MISMATCH FAIL");
                return;
            }

            char logb[96] = {};
            size_t pi = 0;
            while (prefix_log[pi] && pi < 40) { logb[pi] = prefix_log[pi]; pi++; }
            size_t ni = 0;
            while (name[ni] && pi < 70) { logb[pi++] = name[ni++]; }
            const char* suf = juz_istnieje ? " REBOOT PASS" : " WRITE+READ PASS";
            size_t si = 0;
            while (suf[si] && pi < 95) { logb[pi++] = suf[si++]; }
            logb[pi] = '\0';
            wypisz_log(logb);
        };

        // 1. Direct -> Single indirect boundary: 490*4096 = 2007040
        test_plik_strumieniowy(490ULL * 4096ULL - 1ULL, "d_minus", "[PSF-TEST-BOUNDARY-1] ");
        test_plik_strumieniowy(490ULL * 4096ULL,        "d_exact", "[PSF-TEST-BOUNDARY-1] ");
        test_plik_strumieniowy(490ULL * 4096ULL + 1ULL, "d_plus",  "[PSF-TEST-BOUNDARY-1] ");

        // 2. Single indirect -> Double indirect boundary: (490+512)*4096 = 4104192
        test_plik_strumieniowy((490ULL + 512ULL) * 4096ULL - 1ULL, "s_minus", "[PSF-TEST-BOUNDARY-2] ");
        test_plik_strumieniowy((490ULL + 512ULL) * 4096ULL,        "s_exact", "[PSF-TEST-BOUNDARY-2] ");
        test_plik_strumieniowy((490ULL + 512ULL) * 4096ULL + 1ULL, "s_plus",  "[PSF-TEST-BOUNDARY-2] ");

        // 3. Real Large File Streaming Tests
        test_plik_strumieniowy(4ULL * 1024ULL * 1024ULL,   "4M",   "[PSF-TEST-BIG] ");
        test_plik_strumieniowy(32ULL * 1024ULL * 1024ULL,  "32M",  "[PSF-TEST-BIG] ");
        test_plik_strumieniowy(100ULL * 1024ULL * 1024ULL, "100M", "[PSF-TEST-BIG] ");
        test_plik_strumieniowy(126ULL * 1024ULL * 1024ULL, "126M", "[PSF-TEST-BIG] ");
    }
#endif

    importuj_modul_test8k(g_multiboot_info_ptr);
}

/* =========================================================================
 * LOG RAM
 * ========================================================================= */

void wypisz_ilosc_ram() {
    /*
     * Oryginalny kod liczyl:
     * highest_frame * 4096 / 1 MiB.
     *
     * Dzielenie numeru ramki przez 256 daje ten sam wynik bez ryzyka
     * przepelnienia przy mnozeniu.
     */
    const uint64_t ram_mb =
        najwyzsza_znaleziona_ramka / 256ULL;

    char ram_str[32] = {};

    if (!uint_do_str(
            ram_mb,
            ram_str,
            sizeof(ram_str))) {
        wypisz_log(
            "[PMM] Nie mozna sformatowac rozmiaru RAM.");
        return;
    }

    char komunikat[96] = {};

    if (!dolacz_tekst(
            komunikat,
            sizeof(komunikat),
            "[PMM] Skan pamieci fizycznej. Wykryto: ") ||
        !dolacz_tekst(
            komunikat,
            sizeof(komunikat),
            ram_str) ||
        !dolacz_tekst(
            komunikat,
            sizeof(komunikat),
            " MB RAM")) {

        wypisz_log(
            "[PMM] Pamiec fizyczna wykryta.");
        return;
    }

    wypisz_log(komunikat);
}

} // namespace

/* =========================================================================
 * KERNEL MAIN
 * ========================================================================= */

extern "C" void kernel_main(uint64_t multiboot_magic,
                            uint64_t multiboot_info_ptr) {
    /*
     * Cala faza inicjalizacji wykonuje sie atomowo wzgledem IRQ.
     * Szczegolnie wazne jest, aby timer APIC nie uruchomil schedulera
     * zanim zostanie zainstalowany i utworzony pierwszy proces Ring 3.
     */
    asm volatile("cli" ::: "memory");
    serial_inicjalizuj();

    if (multiboot_magic !=
        MULTIBOOT2_BOOTLOADER_MAGIC) {

        zatrzymaj_start(
            "Nieprawidlowy magic Multiboot2.");
    }

    if (multiboot_info_ptr == 0) {
        zatrzymaj_start(
            "Brak wskaznika informacji Multiboot2.");
    }

    g_multiboot_info_ptr = multiboot_info_ptr;

    /* ---------------------------------------------------------------------
     * Fundament CPU i pamieci
     * ------------------------------------------------------------------ */

    InicjalizujPMM(
        multiboot_info_ptr);

    InicjalizujGDT();

    inicjalizuj_tss(
        static_cast<void*>(stack_top));

    zaladuj_tss(
        0x28);

    InicjalizujIDT();

    InicjalizujVMM();

    if (!PobierzAktualnePML4()) {
        zatrzymaj_start(
            "VMM nie udostepnil aktywnego PML4.");
    }

    inicjalizuj_syscalls();

    InicjalizujGrafike(
        multiboot_info_ptr);

    grafika_gotowa = true;

    wypisz_log(
        "==================================================");
    wypisz_log(" Witamy w Bursztyn OS 64-bit ");
    wypisz_log(" Polski System Operacyjny");
    wypisz_log(" Github: Programista-Art/BursztynOS ");
    wypisz_log(" Autor: Dymitr Wygowski ");
    wypisz_log("==================================================");

    wypisz_ilosc_ram();

    wypisz_log(
        "[VMM] Paging 4-poziomowy aktywowany.");

    /* ---------------------------------------------------------------------
     * Sterta jadra
     * ------------------------------------------------------------------ */

    mapuj_nowy_obszar_albo_zatrzymaj(
        ADRES_STERTY_JADRA,
        ROZMIAR_STERTY_JADRA,
        "Brak pamieci fizycznej dla sterty jadra.");

    inicjalizuj_sterte_jadra(
        reinterpret_cast<void*>(
            ADRES_STERTY_JADRA),
        ROZMIAR_STERTY_JADRA);

    /*
     * Prosty test po inicjalizacji. Wykrywa m.in. brak mapowania albo
     * odrzucenie obszaru przez walidacje poprawionego heap.cpp.
     */
    void* test_sterty =
        kmalloc(16);

    if (!test_sterty) {
        zatrzymaj_start(
            "Sterta jadra nie przeszla testu kmalloc.");
    }

    kfree(test_sterty);

    wypisz_log(
        "[HEAP] Sterta jadra 16 MiB gotowa.");

    if (!acpi_inicjalizuj(multiboot_info_ptr))
        wypisz_log("[ACPI] Brak poprawnej tabeli HPET w RSDT/XSDT.");
    else
        wypisz_log(acpi_uzyto_xsdt() ? "[ACPI] Uzywam XSDT." : "[ACPI] Uzywam RSDT.");
    if (hpet_inicjalizuj())
        wypisz_log(hpet_test_wrap_diagnostyczny()
            ? "[HPET-TEST] 32-bit wrap monotonic: OK"
            : "[HPET-TEST] 32-bit wrap monotonic: BLAD");

    /* ---------------------------------------------------------------------
     * Scheduler i kryptografia
     * ------------------------------------------------------------------ */

    void* aktualne_pml4 =
        PobierzAktualnePML4();
    uint64_t aktualne_pml4_fiz = 0;

    if (!WirtualnyDoFizycznego(
            aktualne_pml4,
            &aktualne_pml4_fiz)) {
        zatrzymaj_start(
            "Nie mozna wyznaczyc fizycznego CR3 jadra."
        );
    }

    InicjalizujPlaniste(
        reinterpret_cast<uint64_t>(
            stack_top),
        aktualne_pml4_fiz);

    wypisz_log(
        "[SCHED] PID 0 (idle) przygotowany.");

    inicjalizuj_mbedtls();

    wypisz_log(
        "[KRYPTO] Pula mbedTLS gotowa.");

    /* ---------------------------------------------------------------------
     * PCI i kontrolery przerwan
     * ------------------------------------------------------------------ */

    skanuj_magistrale_pci();

    inicjalizuj_apic();

    wypisz_log(
        "[APIC] LAPIC/IOAPIC uruchomiony.");

    usb_inicjalizuj();

    InicjalizujMyszPS2();

    wypisz_log(
        "[I/O] PS/2 gotowe.");

    wypisz_log(
        "[BWS] API Wywolan Systemowych gotowe.");

    /* ---------------------------------------------------------------------
     * Dysk AHCI i tapeta
     * ------------------------------------------------------------------ */

    inicjalizuj_kontroler_ahci();

    wczytaj_tapete_z_dysku();

    /* ---------------------------------------------------------------------
     * Siec
     * ------------------------------------------------------------------ */

    inicjalizuj_e1000();

    /*
     * Obecny stos E1000/DHCP korzysta z dotychczasowego API void,
     * wiec kernel.cpp nie ma jeszcze wiarygodnej funkcji statusowej
     * informujacej, czy karta zostala odnaleziona.
     */
    uruchom_klienta_dhcp();
#if BURSZTYN_DEBUG_NET_SELFTEST
    wykonaj_selftest_sieci();
#endif

    wypisz_log(
        "[SIEC] Inicjalizacja stosu sieciowego zakonczona.");

    /* ---------------------------------------------------------------------
     * Dzwiek
     * ------------------------------------------------------------------ */

    if (inicjalizuj_hda()) {
        wypisz_log(
            "[HDA] Karta HDA gotowa.");

        hda_test_ton(
            880,
            500);
    } else {
        wypisz_log(
            "[HDA] Brak dostepnego urzadzenia HDA.");
    }

    /* ---------------------------------------------------------------------
     * PSF - osobny, niekolidujacy obszar wirtualny
     * ------------------------------------------------------------------ */

    mapuj_nowy_obszar_albo_zatrzymaj(
        ADRES_RAM_DYSKU_PSF,
        ROZMIAR_RAM_DYSKU_PSF,
        "Brak pamieci fizycznej dla ramdysku PSF.");

    /*
     * PSF nie moze dziedziczyc przypadkowej zawartosci starych ramek PMM.
     */
    wyzeruj_obszar(
        reinterpret_cast<void*>(
            ADRES_RAM_DYSKU_PSF),
        ROZMIAR_RAM_DYSKU_PSF);

    inicjalizuj_psf(
        reinterpret_cast<void*>(
            ADRES_RAM_DYSKU_PSF),
        ROZMIAR_RAM_DYSKU_PSF);

    utworz_drabine_katalogow();

#if BURSZTYN_TESTY_PSF
    wypisz_log("[PSF-BOOT-TEST] create start");
    bool c_ok = utworz_plik("/logi/test.txt");
    wypisz_log(c_ok ? "[PSF-BOOT-TEST] create OK" : "[PSF-BOOT-TEST] create FAIL");
    wypisz_log("[PSF-BOOT-TEST] write start");
    bool w_ok = zapisz_do_pliku("/logi/test.txt", "ABC", 3);
    wypisz_log(w_ok ? "[PSF-BOOT-TEST] write OK" : "[PSF-BOOT-TEST] write FAIL");
    wypisz_log("[PSF-BOOT-TEST] read start");
    char buf_test[8] = {};
    bool r_ok = czytaj_z_pliku("/logi/test.txt", buf_test, 3) && buf_test[0]=='A' && buf_test[1]=='B' && buf_test[2]=='C';
    wypisz_log(r_ok ? "[PSF-BOOT-TEST] read OK" : "[PSF-BOOT-TEST] read FAIL");
#endif

    /* Pierwszy skan wykrywa urzadzenia przed istnieniem PSF. Ten przebieg
       zapisuje raport dopiero po utworzeniu /logi. */
    skanuj_magistrale_pci();

    wypisz_log(
        "[BSP] Drzewiasty system plikow PSF gotowy.");

    /* ---------------------------------------------------------------------
     * Instalacja wbudowanych programow
     * ------------------------------------------------------------------ */

    instaluj_wbudowane_programy();

    /* ---------------------------------------------------------------------
     * Pierwszy proces Ring 3
     * ------------------------------------------------------------------ */

    if (!bws_uruchom_program_z_pliku(
            "/menedzer_okien.bur",
            PZB_USLUGI,
            PRAWA_MENEDZERA_OKIEN,
            false)) {

        zatrzymaj_start(
            "Loader nie utworzyl procesu Menedzera Okien.");
    }

    (void)bws_uruchom_program_z_pliku(
        "/programy/test.cebula/test.bur",
        PZB_UZYTKOWNIK,
        PRAWO_GUI,
        false);

    (void)bws_uruchom_program_z_pliku(
        "/programy/demo_ui.cebula/demo_ui.bur",
        PZB_UZYTKOWNIK,
        PRAWO_GUI,
        false);
    /*
     * Flaga musi zostac ustawiona PRZED STI.
     * Pierwsze przerwanie timera moze od razu wykonac PrzelaczKontekst().
     */
    wielozadaniowosc_aktywna = true;

    wypisz_log(
        "[SCHED] Wielozadaniowosc Round-Robin aktywna.");

    wypisz_log(
        "--------------------------------------------------");
    wypisz_log(
        "System operacyjny gotowy!");

    /*
     * Dopiero teraz dopuszczamy IRQ:
     * - IDT jest gotowe,
     * - APIC jest gotowy,
     * - scheduler jest gotowy,
     * - Menedzer Okien istnieje jako proces Ring 3.
     */
    asm volatile("sti" ::: "memory");

    while (true) {
        usb_obsluz();
        skladacz_obrazu_obsluz_dirty();
        /*
         * STI przed HLT chroni idle przed przypadkowym pozostawieniem IF=0
         * przez kod, ktory w przyszlosci moze wejsc do tej petli.
         */
        asm volatile(
            "sti; hlt"
            :
            :
            : "memory");
    }
}
