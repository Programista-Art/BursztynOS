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

extern "C" uint8_t _binary_kalkulator_bin_start[];
extern "C" uint8_t _binary_kalkulator_bin_end[];

extern "C" uint8_t _binary_menedzer_okien_bin_start[];
extern "C" uint8_t _binary_menedzer_okien_bin_end[];

extern "C" uint8_t _binary_przegladarka_bin_start[];
extern "C" uint8_t _binary_przegladarka_bin_end[];

extern "C" uint8_t _binary_test_bin_start[];
extern "C" uint8_t _binary_test_bin_end[];

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
    2U * 1024U * 1024U;

constexpr uint16_t PORT_COM1 = 0x3F8;

constexpr uint64_t PRAWA_MENEDZERA_OKIEN =
    PRAWO_GUI | PRAWO_URUCHOM_PROGRAM | PRAWO_SYSTEM_CONFIG |
    PRAWO_PLIKI_CZYTAJ | PRAWO_PLIKI_ZAPISZ | PRAWO_SIEC;

bool grafika_gotowa = false;

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

void serial_wypisz(const char* tekst) {
    if (!tekst) return;

    for (size_t i = 0; tekst[i] != '\0'; ++i) {
        serial_outb(
            PORT_COM1,
            static_cast<uint8_t>(tekst[i]));
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
    void* pml4 =
        PobierzAktualnePML4();

    if (!pml4)
        zatrzymaj_start(
            "Brak aktywnego PML4 podczas odswiezania mapowania.");

    asm volatile(
        "mov %0, %%cr3"
        :
        : "r"(pml4)
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
        "/programy/kalkulator.cebula",
        "/programy/przegladarka.cebula",
        "/programy/test.cebula",
        "/uslugi",
        "/sterowniki",
        "/uzytkownicy",
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

    if (multiboot_magic !=
        MULTIBOOT2_BOOTLOADER_MAGIC) {

        zatrzymaj_start(
            "Nieprawidlowy magic Multiboot2.");
    }

    if (multiboot_info_ptr == 0) {
        zatrzymaj_start(
            "Brak wskaznika informacji Multiboot2.");
    }

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

    InicjalizujPlaniste(
        reinterpret_cast<uint64_t>(
            stack_top),
        reinterpret_cast<uint64_t>(
            PobierzAktualnePML4()));

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

#if BURSZTYN_DEBUG_PSF_SELFTEST
    {
        static const char sciezka[] = "/logi/test_kernel.txt";
        static const char wzorzec[] = "ABC123";
        char odczyt[sizeof(wzorzec)] = {};
        if (czytaj_z_pliku(sciezka, odczyt, sizeof(odczyt) - 1U) &&
            odczyt[0] == 'A' && odczyt[1] == 'B' && odczyt[2] == 'C' &&
            odczyt[3] == '1' && odczyt[4] == '2' && odczyt[5] == '3') {
            wypisz_log("[PSF-TEST] persisted read: OK");
        } else {
            (void)utworz_plik(sciezka);
            const bool zapis = zapisz_do_pliku(
                sciezka, wzorzec, static_cast<uint32_t>(sizeof(wzorzec) - 1U));
            wypisz_log(zapis ? "[PSF-TEST] create/write: OK" :
                               "[PSF-TEST] create/write: BLAD");
        }
    }
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
