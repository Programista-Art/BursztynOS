#include "ekran_powitalny_api.h"
#include "boot_phase_timing.h"
#include "opcje_startowe.h"
#include "tapety.h"
#include "ekran.h"
extern "C" uint8_t _binary_ekran_bin_start[], _binary_ekran_bin_end[];
extern "C" uint8_t _binary_tapety_bin_start[], _binary_tapety_bin_end[];
extern "C" void zablokuj_ekran();
extern "C" void odblokuj_ekran();
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
#include "psf_bootstrap.h"
#include "grafika.h"
#include "loader.h"
#include "ahci.h"
#include "instalator.h"
#include "virtio_input_activation_diag.h"
#include "blokowy.h"
#include "e1000.h"
#include "virtio_net.h"
#include "network_device.h"
#include "siec.h"
#include "heap.h"
#include "scheduler.h"
#include "skladacz_obrazu.h"
#include "acpi.h"
#include "sterowniki/czas/hpet.h"
#include "sterowniki/dzwiek/hda.h"
#include "sterowniki/usb/usb.h"
#include "aktualizacja.h"
#include "wersja_systemu.h"
#include "stos_jadra.h"
#include "motyw.h"
#include "virtio_pci.h"
#include "virtqueue.h"
#include "virtio_rng.h"
#include "virtio_blk.h"
#include "virtio_gpu.h"
#include "prezentacja_startu.h"
#include "input_device.h"
#include "virtio_input.h"

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
extern "C" InputDevice* ps2_klawiatura_pobierz_urzadzenie();

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
extern "C" uint8_t _binary_dane_sponsorze_sponsorzy_start[];
extern "C" uint8_t _binary_dane_sponsorze_sponsorzy_end[];

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

extern "C" uint8_t _binary_kontrolki3_bin_start[];
extern "C" uint8_t _binary_kontrolki3_bin_end[];

extern "C" uint8_t _binary_kontrolki4_bin_start[];
extern "C" uint8_t _binary_kontrolki4_bin_end[];

extern "C" uint8_t _binary_aktualizacje_bin_start[];
extern "C" uint8_t _binary_aktualizacje_bin_end[];

extern "C" uint8_t _binary_motywy_bin_start[];
extern "C" uint8_t _binary_motywy_bin_end[];

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

extern "C" uint8_t _binary_obrazy_logo_png_start[];
extern "C" uint8_t _binary_obrazy_logo_png_end[];

/* =========================================================================
 * STALE STARTOWE
 * ========================================================================= */

uint64_t g_multiboot_info_ptr = 0;

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
    128U * 1024U * 1024U;

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

bool scisla_instalacja=false,pliki_instalacja_ok=true;
bool ekran_aplikacja_instalacja_ok=false;
bool tapety_aplikacja_instalacja_ok=false;
struct PlikKontrolny{char path[128];uint32_t size,crc;};
PlikKontrolny pliki_kontrolne[96]{};
unsigned liczba_plikow_kontrolnych=0;
struct PlanowanyPlik{char path[128];const char*data;uint32_t size;};
PlanowanyPlik planowane_pliki[96]{};
unsigned planowane_liczba=0,planowane_indeks=0;
size_t planowane_katalog_indeks=0;
uint32_t planowane_offset=0,planowane_crc=~0U;
uint64_t planowane_bajty=0,planowane_bajty_razem=0;
bool planowanie_instalacji=false,instalacja_krokowa=false;

bool sciezka_ma_poczatek(const char* sciezka,const char* poczatek){
    if(!sciezka||!poczatek)return false;
    size_t i=0;
    while(poczatek[i]&&sciezka[i]==poczatek[i])++i;
    return poczatek[i]=='\0';
}

void log_blad_pliku_instalacji(const char* sciezka,const char* operacja,
                               uint32_t offset=0,uint32_t rozmiar=0){
    const char* skladnik=nullptr;
    if(sciezka_ma_poczatek(sciezka,"/programy/ekran.cebula"))skladnik="[EKRAN]";
    else if(sciezka_ma_poczatek(sciezka,"/programy/tapety.cebula"))skladnik="[TAPETA]";
    if(!skladnik)return;

    char komunikat[256]={};
    char liczba[32]={};
    if(!dolacz_tekst(komunikat,sizeof(komunikat),skladnik)||
       !dolacz_tekst(komunikat,sizeof(komunikat)," install failed ")||
       !dolacz_tekst(komunikat,sizeof(komunikat),operacja)||
       !dolacz_tekst(komunikat,sizeof(komunikat)," ")||
       !dolacz_tekst(komunikat,sizeof(komunikat),sciezka))return;
    if(rozmiar!=0){
        if(!uint_do_str(offset,liczba,sizeof(liczba))||
           !dolacz_tekst(komunikat,sizeof(komunikat)," offset=")||
           !dolacz_tekst(komunikat,sizeof(komunikat),liczba)||
           !uint_do_str(rozmiar,liczba,sizeof(liczba))||
           !dolacz_tekst(komunikat,sizeof(komunikat)," size=")||
           !dolacz_tekst(komunikat,sizeof(komunikat),liczba))return;
    }
    wypisz_log(komunikat);
}
bool zapisz_plik_od_nowa_impl(const char* sciezka,
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

bool zapisz_plik_od_nowa(const char*path,const char*data,uint32_t size){
    if(planowanie_instalacji){
        if(!path||(size&&!data)||planowane_liczba>=96||planowane_bajty_razem>UINT64_MAX-size){
            pliki_instalacja_ok=false;return false;
        }
        auto&e=planowane_pliki[planowane_liczba++];unsigned i=0;
        for(;i+1<sizeof(e.path)&&path[i];++i){e.path[i]=path[i];}
        e.path[i]=0;
        if(path[i]){pliki_instalacja_ok=false;return false;}
        e.data=data;e.size=size;planowane_bajty_razem+=size;
        return true;
    }
    if(scisla_instalacja&&!pliki_instalacja_ok)return false;
    bool ok=zapisz_plik_od_nowa_impl(path,data,size);
    if(scisla_instalacja){
        if(!ok||liczba_plikow_kontrolnych>=96){pliki_instalacja_ok=false;return false;}
        auto&e=pliki_kontrolne[liczba_plikow_kontrolnych++];unsigned i=0;
        for(;i+1<sizeof(e.path)&&path[i];++i){
            e.path[i]=path[i];
        }
        e.path[i]=0;
        if(path[i]){pliki_instalacja_ok=false;return false;}
        e.size=size;e.crc=Storage::crc32(data,size);
    }
    return ok;
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


void utworz_drabine_katalogow() {

    for (size_t i = 0;
         i < sizeof(KATALOGI_SYSTEMOWE) / sizeof(KATALOGI_SYSTEMOWE[0]);
         ++i) {

        PsfStanStartu stan{};
        const bool ok=zapewnij_katalog_startowy(KATALOGI_SYSTEMOWE[i],&stan);
        psf_start_log(i,KATALOGI_SYSTEMOWE[i],!instalator_system_zainstalowany(),stan);
        if (!ok) {
            if(scisla_instalacja){pliki_instalacja_ok=false;return;}
            /*
             * Dopuszczamy katalogi odziedziczone z trwalego BSP2, ale nadal
             * zatrzymujemy start, jezeli wymaganej sciezki nie mozna ani
             * odczytac jako katalogu, ani poprawnie utworzyc.
             */
            char komunikat[192]{};
            psf_start_panic(komunikat,sizeof(komunikat),KATALOGI_SYSTEMOWE[i],stan);
            zatrzymaj_start(komunikat);
        }
    }
}

/* =========================================================================
 * IMPORT MODULOW MULTIBOOT2 (test8k.bmp)
 * ========================================================================= */

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

bool zadano_automatyczne_testy(uint64_t mbi, const char* token = "bursztyn.test=automated") {
    if (mbi == 0) return false;

    const volatile uint32_t* info =
        reinterpret_cast<const volatile uint32_t*>(mbi);
    const uint32_t calosc = info[0];
    if (calosc < 16 || mbi > UINT64_MAX - calosc) return false;

    uint64_t off = 8;
    while (off + sizeof(WpisTaguMB2) <= calosc) {
        const WpisTaguMB2* tag =
            reinterpret_cast<const WpisTaguMB2*>(mbi + off);
        if (tag->rozmiar < sizeof(WpisTaguMB2) ||
            off + tag->rozmiar > calosc) {
            return false;
        }

        if (tag->typ == 1 /* MULTIBOOT_TAG_TYPE_CMDLINE */ &&
            tag->rozmiar > sizeof(WpisTaguMB2)) {
            const char* cmdline = reinterpret_cast<const char*>(mbi + off + 8);
            const uint32_t pojemnosc = tag->rozmiar - 8;
            for (uint32_t i = 0; i < pojemnosc; ++i) {
                if (cmdline[i] == '\0') {
                    return opcja_startowa_obecna(cmdline, token);
                }
            }
            return false;
        }

        if (tag->typ == MULTIBOOT_TAG_TYPE_END) break;
        off = (off + tag->rozmiar + 7ULL) & ~7ULL;
    }
    return false;
}

bool zadano_test_stack_guard(uint64_t mbi) {
    if (mbi == 0) return false;

    const volatile uint32_t* info =
        reinterpret_cast<const volatile uint32_t*>(mbi);
    const uint32_t calosc = info[0];
    if (calosc < 16 || mbi > UINT64_MAX - calosc) return false;

    uint64_t off = 8;
    while (off + sizeof(WpisTaguMB2) <= calosc) {
        const WpisTaguMB2* tag =
            reinterpret_cast<const WpisTaguMB2*>(mbi + off);
        if (tag->rozmiar < sizeof(WpisTaguMB2) ||
            off + tag->rozmiar > calosc) {
            return false;
        }

        if (tag->typ == 1 /* MULTIBOOT_TAG_TYPE_CMDLINE */ &&
            tag->rozmiar > sizeof(WpisTaguMB2)) {
            const char* cmdline = reinterpret_cast<const char*>(mbi + off + 8);
            const uint32_t pojemnosc = tag->rozmiar - 8;
            for (uint32_t i = 0; i < pojemnosc; ++i) {
                if (cmdline[i] == '\0') {
                    return tekst_zawiera(
                        cmdline, "bursztyn.test=stack_guard");
                }
            }
            return false;
        }

        if (tag->typ == MULTIBOOT_TAG_TYPE_END) break;
        off = (off + tag->rozmiar + 7ULL) & ~7ULL;
    }
    return false;
}

bool zadano_destrukcyjne_testy_dysku(uint64_t mbi) {
    if (mbi == 0) return false;

    const volatile uint32_t* info =
        reinterpret_cast<const volatile uint32_t*>(mbi);
    const uint32_t calosc = info[0];
    if (calosc < 16 || mbi > UINT64_MAX - calosc) return false;

    uint64_t off = 8;
    while (off + sizeof(WpisTaguMB2) <= calosc) {
        const WpisTaguMB2* tag =
            reinterpret_cast<const WpisTaguMB2*>(mbi + off);
        if (tag->rozmiar < sizeof(WpisTaguMB2) ||
            off + tag->rozmiar > calosc) {
            return false;
        }

        if (tag->typ == 1 /* MULTIBOOT_TAG_TYPE_CMDLINE */ &&
            tag->rozmiar > sizeof(WpisTaguMB2)) {
            const char* cmdline = reinterpret_cast<const char*>(mbi + off + 8);
            const uint32_t pojemnosc = tag->rozmiar - 8;
            for (uint32_t i = 0; i < pojemnosc; ++i) {
                if (cmdline[i] == '\0') {
                    return tekst_zawiera(
                        cmdline, "bursztyn.test.diskdestructive=1");
                }
            }
            return false;
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
        "nazwa = \"Testy systemu\"\n"
        "autor = \"Programista Art\"\n"
        "wersja = \"1.0\"\n"
        "poziom_zaufania = 4\n"
        "plik_startowy = \"test.bur\"\n"
        "uprawnienia = [\n"
        "    \"okna\",\n"
        "    \"uruchom_program\",\n"
        "    \"pliki_czytaj\"\n"
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

    static const char MANIFEST_EKRAN[] =
        "nazwa = \"Ustawienia ekranu\"\n"
        "autor = \"Bursztyn OS\"\n"
        "wersja = \"1.0\"\n"
        "poziom_zaufania = 3\n"
        "plik_startowy = \"ekran.bur\"\n"
        "uprawnienia = [\"okna\", \"system_config\"]\n";

    static const char MANIFEST_TAPETY[] =
        "nazwa = \"Tapety\"\n"
        "autor = \"Bursztyn OS\"\n"
        "wersja = \"1.0\"\n"
        "poziom_zaufania = 4\n"
        "plik_startowy = \"tapety.bur\"\n"
        "uprawnienia = [\n    \"okna\",\n    \"pliki_czytaj\"\n]\n";

    static const char MANIFEST_KONTROLKI3[] =
        "nazwa = \"Kontrolki 3\"\n"
        "autor = \"Programista Art\"\n"
        "wersja = \"0.1\"\n"
        "poziom_zaufania = 4\n"
        "plik_startowy = \"kontrolki3.bur\"\n"
        "uprawnienia = [\n"
        "    \"okna\"\n"
        "]\n";

    static const char MANIFEST_KONTROLKI4[] =
        "nazwa = \"Kontrolki 4\"\n"
        "autor = \"Programista Art\"\n"
        "wersja = \"0.1\"\n"
        "poziom_zaufania = 4\n"
        "plik_startowy = \"kontrolki4.bur\"\n"
        "uprawnienia = [\n"
        "    \"okna\",\n"
        "    \"pliki_czytaj\",\n"
        "    \"pliki_zapisz\"\n"
        "]\n";

    static const char MANIFEST_AKTUALIZACJE[] =
        "nazwa = \"Aktualizacje Systemu\"\n"
        "autor = \"Programista Art\"\n"
        "wersja = \"0.1\"\n"
        "poziom_zaufania = 2\n"
        "plik_startowy = \"aktualizacje.bur\"\n"
        "uprawnienia = [\n"
        "    \"okna\",\n"
        "    \"pliki_czytaj\",\n"
        "    \"pliki_zapisz\",\n"
        "    \"system_config\",\n"
        "    \"siec\"\n"
        "]\n";

    static const char MANIFEST_MOTYWY[] =
        "nazwa = \"Motywy Systemowe\"\n"
        "autor = \"Programista Art\"\n"
        "wersja = \"1.0\"\n"
        "poziom_zaufania = 3\n"
        "plik_startowy = \"motywy.bur\"\n"
        "uprawnienia = [\n"
        "    \"okna\",\n"
        "    \"pliki_czytaj\",\n"
        "    \"pliki_zapisz\",\n"
        "    \"system_config\"\n"
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
        if(scisla_instalacja){pliki_instalacja_ok=false;return;}
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

    uint32_t sponsorzy_rozmiar=0;
    if(!pobierz_rozmiar_bloba(_binary_dane_sponsorze_sponsorzy_start,
        _binary_dane_sponsorze_sponsorzy_end,&sponsorzy_rozmiar) ||
       !zapisz_plik_od_nowa("/dane/sponsorze/sponsorzy",
        reinterpret_cast<const char*>(_binary_dane_sponsorze_sponsorzy_start),sponsorzy_rozmiar)) {
        wypisz_log("[BSP-BLAD] Nie mozna zainstalowac listy sponsorow.");
        if(scisla_instalacja) pliki_instalacja_ok=false;
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
        (void)zapisz_plik_od_nowa(
            sciezka,
            reinterpret_cast<const char*>(blob),
            sz);
#endif
        (void)tag;
    };
    instaluj_bmp("/uzytkownicy/Pulpit/test.bmp", _binary_obrazy_test_bmp_start, (uint32_t)test_bmp_rozmiar, "test");
    instaluj_bmp("/uzytkownicy/Pulpit/test1.bmp", _binary_obrazy_test1_bmp_start, (uint32_t)test1_bmp_rozmiar, "test1");

    const uint64_t logo_png_rozmiar =
        static_cast<uint64_t>(
            _binary_obrazy_logo_png_end -
            _binary_obrazy_logo_png_start);
    if (logo_png_rozmiar > 0 && logo_png_rozmiar <= UINT32_MAX) {
        if (zapisz_plik_od_nowa("/obrazy/logo.png",
                reinterpret_cast<const char*>(_binary_obrazy_logo_png_start),
                static_cast<uint32_t>(logo_png_rozmiar))) {
            wypisz_log("[BSP] /obrazy/logo.png dodany do planu instalacji.");
        }
    }

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

    ekran_aplikacja_instalacja_ok=instaluj_paczke(
        "/programy/ekran.cebula/opis.aplikacji",MANIFEST_EKRAN,
        EKRAN_PROGRAM,_binary_ekran_bin_start,_binary_ekran_bin_end);
    if(!ekran_aplikacja_instalacja_ok){
        log_blad_pliku_instalacji(EKRAN_PROGRAM,"plan/package");
        if(scisla_instalacja)pliki_instalacja_ok=false;
    }else{
        wypisz_log(planowanie_instalacji
            ? "[EKRAN] Aplikacja dodana do planu instalacji."
            : "[EKRAN] Aplikacja zainstalowana.");
    }

    tapety_aplikacja_instalacja_ok=instaluj_paczke(
        "/programy/tapety.cebula/opis.aplikacji",MANIFEST_TAPETY,
        TAPETY_PROGRAM,_binary_tapety_bin_start,_binary_tapety_bin_end);
    if(!tapety_aplikacja_instalacja_ok){
        log_blad_pliku_instalacji(TAPETY_PROGRAM,"plan/package");
        if(scisla_instalacja)pliki_instalacja_ok=false;
    }else{
        wypisz_log(planowanie_instalacji
            ? "[TAPETA] Aplikacja Tapety dodana do planu instalacji."
            : "[TAPETA] Aplikacja Tapety zainstalowana.");
    }

    if (!instaluj_paczke(
            "/programy/kontrolki3.cebula/opis.aplikacji",
            MANIFEST_KONTROLKI3,
            "/programy/kontrolki3.cebula/kontrolki3.bur",
            _binary_kontrolki3_bin_start,
            _binary_kontrolki3_bin_end)) {
        wypisz_log(
            "[BSP-BLAD] Nie mozna zainstalowac Kontrolek 3.");
    } else {
        wypisz_log(
            "[BSP] Kontrolki 3 zainstalowane jako .cebula.");
    }

    if (!instaluj_paczke(
            "/programy/kontrolki4.cebula/opis.aplikacji",
            MANIFEST_KONTROLKI4,
            "/programy/kontrolki4.cebula/kontrolki4.bur",
            _binary_kontrolki4_bin_start,
            _binary_kontrolki4_bin_end)) {
        wypisz_log(
            "[BSP-BLAD] Nie mozna zainstalowac Kontrolek 4.");
    } else {
        wypisz_log(
            "[BSP] Kontrolki 4 zainstalowane jako .cebula.");
    }

    if (!instaluj_paczke(
            "/programy/aktualizacje.cebula/opis.aplikacji",
            MANIFEST_AKTUALIZACJE,
            "/programy/aktualizacje.cebula/aktualizacje.bur",
            _binary_aktualizacje_bin_start,
            _binary_aktualizacje_bin_end)) {
        wypisz_log(
            "[BSP-BLAD] Nie mozna zainstalowac Aktualizacji Systemu.");
    } else {
        wypisz_log(
            "[BSP] Aktualizacje Systemu zainstalowane jako .cebula.");
    }

    if (!instaluj_paczke(
            "/programy/motywy.cebula/opis.aplikacji",
            MANIFEST_MOTYWY,
            "/programy/motywy.cebula/motywy.bur",
            _binary_motywy_bin_start,
            _binary_motywy_bin_end)) {
        wypisz_log(
            "[BSP-BLAD] Nie mozna zainstalowac Motywow Systemowych.");
    } else {
        wypisz_log(
            "[BSP] Motywy Systemowe zainstalowane jako .cebula.");
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
        if(scisla_instalacja){pliki_instalacja_ok=false;return;}
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

    bool kernel_instaluj_system(){
    scisla_instalacja=true;pliki_instalacja_ok=true;liczba_plikow_kontrolnych=0;
    ekran_aplikacja_instalacja_ok=tapety_aplikacja_instalacja_ok=false;
    utworz_drabine_katalogow();
    aktualizacja_przygotuj_system_plikow();
    if(pliki_instalacja_ok)instaluj_wbudowane_programy();
    scisla_instalacja=false;
    return pliki_instalacja_ok&&ekran_aplikacja_instalacja_ok&&
           tapety_aplikacja_instalacja_ok&&liczba_plikow_kontrolnych!=0&&psf_flush_cache();
}
bool kernel_instaluj_system_start(uint64_t* razem){
    scisla_instalacja=true;pliki_instalacja_ok=true;liczba_plikow_kontrolnych=0;
    ekran_aplikacja_instalacja_ok=tapety_aplikacja_instalacja_ok=false;
    planowane_liczba=planowane_indeks=0;planowane_katalog_indeks=0;planowane_offset=0;planowane_crc=~0U;
    planowane_bajty=planowane_bajty_razem=0;instalacja_krokowa=false;
    planowanie_instalacji=true;
    if(pliki_instalacja_ok)instaluj_wbudowane_programy();
    planowanie_instalacji=false;
    instalacja_krokowa=pliki_instalacja_ok&&ekran_aplikacja_instalacja_ok&&
        tapety_aplikacja_instalacja_ok&&planowane_liczba!=0;
    if(razem)*razem=planowane_bajty_razem;
    return instalacja_krokowa;
}
bool kernel_instaluj_system_step(uint64_t* wykonane,uint32_t* pliki,uint32_t* wszystkie,bool* gotowe){
    if(wykonane)*wykonane=planowane_bajty;
    if(pliki)*pliki=planowane_indeks;
    if(wszystkie)*wszystkie=planowane_liczba;
    if(gotowe)*gotowe=false;
    if(!instalacja_krokowa||!pliki_instalacja_ok)return false;
    if(planowane_indeks==0&&planowane_offset==0&&planowane_bajty==0&&planowane_katalog_indeks<sizeof(KATALOGI_SYSTEMOWE)/sizeof(KATALOGI_SYSTEMOWE[0])){
        const char* katalog=KATALOGI_SYSTEMOWE[planowane_katalog_indeks];
        if(!zapewnij_katalog_startowy(katalog)){
            log_blad_pliku_instalacji(katalog,"mkdir error=psf-create-or-verify");
            planowane_katalog_indeks=0;pliki_instalacja_ok=false;return false;
        }
        if(!psf_flush_cache()){
            log_blad_pliku_instalacji(katalog,"mkdir-flush error=psf-flush");
            planowane_katalog_indeks=0;pliki_instalacja_ok=false;return false;
        }
        ++planowane_katalog_indeks;
        return true;
    }
    if(planowane_indeks==planowane_liczba){
        const bool ok=psf_flush_cache();scisla_instalacja=false;instalacja_krokowa=false;
        if(gotowe)*gotowe=ok;
        return ok;
    }
    auto&e=planowane_pliki[planowane_indeks];
    if(planowane_offset==0){
        (void)usun_twor(e.path);
        if(!utworz_plik(e.path)){
            log_blad_pliku_instalacji(e.path,"create");
            pliki_instalacja_ok=false;return false;
        }
    }
    constexpr uint32_t PORCJA=4*1024;
    static_assert(PORCJA<=4096,"SYSTEMFILES STEP nie moze przekroczyc jednego bloku BSP2");
    uint32_t n=e.size-planowane_offset;if(n>PORCJA)n=PORCJA;
    if(n&&!zapisz_fragment_pliku(e.path,planowane_offset,e.data+planowane_offset,n)){
        log_blad_pliku_instalacji(e.path,"write",planowane_offset,n);
        (void)usun_twor(e.path);pliki_instalacja_ok=false;return false;
    }
    if(!psf_flush_cache()){
        log_blad_pliku_instalacji(e.path,"flush",planowane_offset,n);
        (void)usun_twor(e.path);pliki_instalacja_ok=false;return false;
    }
    for(uint32_t i=0;i<n;++i){
        planowane_crc^=uint8_t(e.data[planowane_offset+i]);
        for(unsigned bit=0;bit<8;++bit)planowane_crc=(planowane_crc>>1)^(0xEDB88320U&(0U-(planowane_crc&1)));
    }
    planowane_offset+=n;planowane_bajty+=n;
    if(planowane_offset==e.size){
        if(liczba_plikow_kontrolnych>=96){pliki_instalacja_ok=false;return false;}
        auto&k=pliki_kontrolne[liczba_plikow_kontrolnych++];
        unsigned i=0;for(;i+1<sizeof(k.path)&&e.path[i];++i){k.path[i]=e.path[i];}k.path[i]=0;
        k.size=e.size;k.crc=~planowane_crc;
        ++planowane_indeks;planowane_offset=0;planowane_crc=~0U;
    }
    if(wykonane)*wykonane=planowane_bajty;
    if(pliki)*pliki=planowane_indeks;
    return true;
}
uint32_t kernel_liczba_plikow_instalacji(){return liczba_plikow_kontrolnych;}
bool kernel_weryfikuj_system(){
    char buf[4096];
    if(!pliki_instalacja_ok||!ekran_aplikacja_instalacja_ok||
       !tapety_aplikacja_instalacja_ok||liczba_plikow_kontrolnych==0)return false;
    for(unsigned i=0;i<liczba_plikow_kontrolnych;++i){const auto&e=pliki_kontrolne[i];
        if(rozmiar_pliku(e.path)!=e.size)return false;
        uint32_t c=~0U;
        for(uint32_t off=0;off<e.size;){uint32_t n=e.size-off>sizeof(buf)?sizeof(buf):e.size-off,got=0;
            if(!czytaj_fragment_pliku(e.path,off,buf,n,&got)||got!=n)return false;
            for(uint32_t j=0;j<n;++j){c^=uint8_t(buf[j]);for(unsigned k=0;k<8;++k)c=(c>>1)^(0xEDB88320U&(0U-(c&1)));}off+=n;}
        if(~c!=e.crc)return false;
    }
    return true;
}
unsigned weryfikacja_indeks=0;
uint32_t weryfikacja_offset=0,weryfikacja_crc=~0U;
char weryfikacja_buf[4*1024];
bool kernel_weryfikuj_system_start(uint64_t* razem){
    if(!pliki_instalacja_ok||!ekran_aplikacja_instalacja_ok||
       !tapety_aplikacja_instalacja_ok||liczba_plikow_kontrolnych==0)return false;
    uint64_t total=0;for(unsigned i=0;i<liczba_plikow_kontrolnych;++i){
        if(total>UINT64_MAX-pliki_kontrolne[i].size)return false;
        total+=pliki_kontrolne[i].size;
    }
    weryfikacja_indeks=0;weryfikacja_offset=0;weryfikacja_crc=~0U;if(razem)*razem=total;return true;
}
bool kernel_weryfikuj_system_step(uint64_t* wykonane,bool* gotowe){
    static uint64_t verified=0;
    if(weryfikacja_indeks==0&&weryfikacja_offset==0)verified=0;
    if(wykonane)*wykonane=verified;
    if(gotowe)*gotowe=false;
    if(weryfikacja_indeks>=liczba_plikow_kontrolnych){if(gotowe)*gotowe=true;return true;}
    const auto&e=pliki_kontrolne[weryfikacja_indeks];
    if(weryfikacja_offset==0&&rozmiar_pliku(e.path)!=e.size){
        log_blad_pliku_instalacji(e.path,"verify-size error=mismatch");
        return false;
    }
    uint32_t n=e.size-weryfikacja_offset;if(n>sizeof(weryfikacja_buf))n=sizeof(weryfikacja_buf);uint32_t got=0;
    if(n&&(!czytaj_fragment_pliku(e.path,weryfikacja_offset,weryfikacja_buf,n,&got)||got!=n)){
        log_blad_pliku_instalacji(e.path,"verify-read",weryfikacja_offset,n);
        return false;
    }
    for(uint32_t i=0;i<n;++i){weryfikacja_crc^=uint8_t(weryfikacja_buf[i]);for(unsigned bit=0;bit<8;++bit)weryfikacja_crc=(weryfikacja_crc>>1)^(0xEDB88320U&(0U-(weryfikacja_crc&1)));}
    weryfikacja_offset+=n;verified+=n;
    if(weryfikacja_offset==e.size){
        if(~weryfikacja_crc!=e.crc){
            log_blad_pliku_instalacji(e.path,"verify-crc error=mismatch");
            return false;
        }
        ++weryfikacja_indeks;weryfikacja_offset=0;weryfikacja_crc=~0U;
    }
    if(wykonane)*wykonane=verified;
    if(weryfikacja_indeks==liczba_plikow_kontrolnych&&gotowe)*gotowe=true;
    return true;
}

static uint64_t t_kernel_ready_us = 0;
static bool system_pulpit_gotowy = false;

extern "C" bool system_potwierdz_gotowosc_pulpitu(int pid) {
    if (pid <= 0 || pid >= static_cast<int>(MAKS_PROCESOW)) {
        return false;
    }

    // Walidacja aktywnej, widocznej warstwy bazowej pulpitu nalezacej do procesu
    warstwa_obrazu* w = pobierz_warstwe(pid);
    if (!w) return false;
    if (w->pid != pid) return false;
    if (!w->aktywna) return false;
    if (w->z_order != 0) return false;
    if (w->x != 0 || w->y != 0) return false;
    if (w->bufor_pikseli == nullptr) return false;
    if (!skladacz_obrazu_czy_widoczna(pid)) return false;

    const int screen_w = grafika_pobierz_szerokosc();
    const int screen_h = grafika_pobierz_wysokosc();
    if (screen_w <= 0 || screen_h <= 0) return false;
    if (w->szerokosc < screen_w || w->wysokosc < screen_h) return false;

    // Idempotentny rygiel: CAS zapewnia, ze przejscie w stan gotowosci nastapi dokladnie raz
    bool oczekiwana = false;
    if (!__atomic_compare_exchange_n(&system_pulpit_gotowy, &oczekiwana, true,
                                      false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        // Pulpit zostal juz potwierdzony; powtorne wywolanie przez uprawniony proces to idempotentny no-op.
        return true;
    }

    powitanie_pulpit_gotowy(grafika_czy_tapeta(),pid);

    // Dokoncz pierwsze pelne zlozenie i flush pulpitu na scanout
    zablokuj_ekran();
    skladacz_obrazu_obsluz_dirty();
    odblokuj_ekran();

    const uint64_t t_desktop_ready_us = hpet_dostepny() ? czas_monotoniczny_us() : 0;
    const uint64_t delta_us = (t_desktop_ready_us >= t_kernel_ready_us && t_kernel_ready_us > 0)
                                  ? (t_desktop_ready_us - t_kernel_ready_us)
                                  : 0;

    char tel[128] = "[BOOT-TELEMETRY] kernel_ready_time_us=";
    char num[32] = {};
    uint_do_str(t_kernel_ready_us, num, sizeof(num));
    dolacz_tekst(tel, sizeof(tel), num);
    dolacz_tekst(tel, sizeof(tel), " desktop_ready_time_us=");
    uint_do_str(t_desktop_ready_us, num, sizeof(num));
    dolacz_tekst(tel, sizeof(tel), num);
    dolacz_tekst(tel, sizeof(tel), " delta_us=");
    uint_do_str(delta_us, num, sizeof(num));
    dolacz_tekst(tel, sizeof(tel), num);
    wypisz_log(tel);

    wypisz_log("[SYSTEM] Pulpit gotowy.");
    wypisz_log("System operacyjny gotowy!");
    aktualizacja_potwierdz_rozruch();

    return true;
}

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
    BootTiming::entry();
    asm volatile("cli" ::: "memory");
    serial_inicjalizuj();
#if defined(BURSZTYN_DIAG_VIRTIO_BOOT) && BURSZTYN_DIAG_VIRTIO_BOOT
    klog_serial("[VIRTIO RESET TRACE] kernel entry");
#endif

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
#if defined(BURSZTYN_DIAG_PCI_TRACE) && BURSZTYN_DIAG_PCI_TRACE
    {
        const volatile uint32_t* info =
            reinterpret_cast<const volatile uint32_t*>(multiboot_info_ptr);
        const uint32_t total = info[0];
        uint64_t off = 8;
        bool logged = false;
        while (off + sizeof(WpisTaguMB2) <= total) {
            const WpisTaguMB2* tag =
                reinterpret_cast<const WpisTaguMB2*>(multiboot_info_ptr + off);
            if (tag->rozmiar < sizeof(WpisTaguMB2) || off + tag->rozmiar > total)
                break;
            if (tag->typ == 1 && tag->rozmiar > 8) {
                klog_serial("[BOOT TRACE] Multiboot2 command line:");
                klog_serial(reinterpret_cast<const char*>(multiboot_info_ptr + off + 8));
                logged = true;
                break;
            }
            if (tag->typ == MULTIBOOT_TAG_TYPE_END) break;
            off = (off + tag->rozmiar + 7ULL) & ~7ULL;
        }
        if (!logged) klog_serial("[BOOT TRACE] Multiboot2 command line: <none>");
    }
#endif
    instalator_boot(multiboot_info_ptr);

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
    BootTiming::framebuffer_ready();

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

    if (!stos_jadra_inicjalizuj()) {
        zatrzymaj_start(
            "Nie udalo sie zainicjalizowac podsystemu stosow jadra.");
    }

    if (!acpi_inicjalizuj(multiboot_info_ptr))
        wypisz_log("[ACPI] Brak poprawnej tabeli HPET w RSDT/XSDT.");
    else
        wypisz_log(acpi_uzyto_xsdt() ? "[ACPI] Uzywam XSDT." : "[ACPI] Uzywam RSDT.");
    if (hpet_inicjalizuj())
        wypisz_log(hpet_test_wrap_diagnostyczny()
            ? "[HPET-TEST] 32-bit wrap monotonic: OK"
            : "[HPET-TEST] 32-bit wrap monotonic: BLAD");

    BootTiming::clock_ready();
    powitanie_inicjalizuj(
        !zadano_automatyczne_testy(g_multiboot_info_ptr) &&
        !zadano_automatyczne_testy(g_multiboot_info_ptr, "bursztyn.splash=0") &&
        !zadano_automatyczne_testy(g_multiboot_info_ptr, "bursztyn.install=1"));

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

    {BootTiming::Scope timing(BootTiming::PciScan);skanuj_magistrale_pci();}

    inicjalizuj_apic();

    wypisz_log(
        "[APIC] LAPIC/IOAPIC uruchomiony.");

    input_set_pointer_ready(false);
    (void)ps2_klawiatura_pobierz_urzadzenie();
    // Complete USB firmware handoff/controller setup before negotiating the
    // i8042 streaming protocol, as in the known-working hardware sequence.
    // USB registration stays ineligible until a real valid runtime report.
    {BootTiming::Scope timing(BootTiming::UsbBeforeSplash);usb_inicjalizuj();}
    {BootTiming::Scope timing(BootTiming::Ps2Init);InicjalizujMyszPS2();}

    wypisz_log(
        "[I/O] PS/2 gotowe.");

    wypisz_log(
        "[BWS] API Wywolan Systemowych gotowe.");

    /* ---------------------------------------------------------------------
     * Podsystem blokowy i dyski pamieci masowej (AHCI)
     * ------------------------------------------------------------------ */

    blokowy_inicjalizuj();
    inicjalizuj_kontroler_ahci();
    virtio_pci_inicjalizuj();

    const bool tryb_testowy = zadano_automatyczne_testy(g_multiboot_info_ptr);
    const bool tryb_destrukcyjny_dysk = tryb_testowy && zadano_destrukcyjne_testy_dysku(g_multiboot_info_ptr);
    if (!tryb_testowy) {
        wypisz_log("[NORMAL BOOT] VirtIO self-tests skipped.");
    }

    virtio_pci_diag_trace_type(VIRTIO_ID_RNG, "driver probe begin");
    if (virtio_rng_probe_and_init()) {
        VirtioRngDriver* rng_drv = virtio_rng_get_driver();
        if (rng_drv && rng_drv->dev) {
            if (tryb_testowy) {
                virtio_rng_stress_test(rng_drv->dev);
            } else {
                uint8_t entropy_check[16]{};
                size_t n = virtio_rng_read(entropy_check, sizeof(entropy_check));
                if (n == sizeof(entropy_check)) {
                    wypisz_log("[VIRTIO-RNG] Sanity check: OK (16B entropy read).");
                }
            }
        }
    }
    virtio_pci_diag_trace_type(VIRTIO_ID_RNG, "driver probe end");
    virtio_pci_diag_trace_type(VIRTIO_ID_BLOCK, "driver probe begin");
    const uint32_t virtio_blk_count = virtio_blk_probe_and_init();
    if (virtio_blk_count > 0) {
        VirtioBlkDriver* blk_drv = virtio_blk_get_driver(0);
        if (blk_drv && blk_drv->initialized) {
            if (tryb_testowy) {
                virtio_blk_stress_test(blk_drv, tryb_destrukcyjny_dysk);
            } else {
                uint8_t sec0[512]{};
                BlockError err = blk_drv->bdev.read(0, 1, sec0);
                if (err == BlockError::Ok) {
                    wypisz_log("[VIRTIO-BLK] Sanity check: OK (read LBA 0).");
                }
            }
        }
    }
    virtio_pci_diag_trace_type(VIRTIO_ID_BLOCK, "driver probe end");

    virtio_pci_diag_trace_type(VIRTIO_ID_GPU, "driver probe begin");
    const uint32_t virtio_gpu_count = virtio_gpu_probe_and_init();
    if (virtio_gpu_count > 0) {
        VirtioGpuDriver* gpu_drv = virtio_gpu_get_driver(0);
        if (gpu_drv && gpu_drv->initialized) {
            if (tryb_testowy) {
                // Explicit diagnostic patterns own their complete test frame.
                PrezentacjaStartu::wybierz_backend();PrezentacjaStartu::przygotowana();
                virtio_gpu_test_teardown_reinit(gpu_drv);
                gpu_drv = virtio_gpu_get_driver(0);
                if (gpu_drv && gpu_drv->initialized) {
                    virtio_gpu_stress_dirty_rectangles(gpu_drv);
                    virtio_gpu_render_test_pattern(gpu_drv);
                    wypisz_log("[VIRTIO-GPU-FINAL] Finalny wzorzec testowy gotowy do weryfikacji pikseli.");
                    hpet_czekaj_ns_boot(300000000ULL);
                    virtio_gpu_test_runtime_modeset(gpu_drv);
                    grafika_aktywuj_virtio_gpu(gpu_drv);
                    grafika_test_kursor_suite(gpu_drv);
                }
            } else {
                grafika_aktywuj_virtio_gpu(gpu_drv);
            }
        }
    }
    grafika_zakoncz_wybor_backendu();
    BootTiming::dump();
    virtio_pci_diag_trace_type(VIRTIO_ID_GPU, "driver probe end");

    /* BMP pozostaje fallbackiem po inicjalizacji BSP2. */

    /* ---------------------------------------------------------------------
     * Siec
     * ------------------------------------------------------------------ */

    inicjalizuj_e1000();
    virtio_pci_diag_trace_type(VIRTIO_ID_NET, "driver probe begin");
    const uint32_t virtio_net_count = virtio_net_probe_and_init();
    if (virtio_net_count > 0) {
        VirtioNetDriver* net_drv = virtio_net_get_driver(0);
        if (net_drv && net_drv->initialized) {
            if (tryb_testowy) {
                virtio_net_stress_test(net_drv);
            }
        }
    }
    virtio_pci_diag_trace_type(VIRTIO_ID_NET, "driver probe end");

    virtio_pci_diag_trace_type(VIRTIO_ID_INPUT, "driver probe begin");
#if BURSZTYNDIAGVIRTIOINPUTINIT
    input_activation_log("boot installer mode", instalator_tryb());
    input_activation_log("boot Multiboot address", g_multiboot_info_ptr);
    input_activation_log("boot scheduler PID", aktualny_pid);
#endif
    const uint32_t virtio_input_prepared = virtio_input_probe_and_prepare();
    if (virtio_input_prepared) wypisz_log("[VIRTIO-INPUT] Devices PREPARED; activation deferred until kernel bootstrap completes.");
    virtio_pci_diag_trace_type(VIRTIO_ID_INPUT, "driver probe end");

    NetworkDevice* net_dev = net_get_primary_device();
    if (net_dev) {
        uruchom_klienta_dhcp();
        if (brama_ip[0] != 0 || brama_ip[1] != 0 || brama_ip[2] != 0 || brama_ip[3] != 0) {
            uint8_t brama_mac[6] = {};
            if (rozwiaz_adres_mac(brama_ip, brama_mac)) {
                wypisz_log("[SIEC] Brama domyslna (ARP) rozwiazana pomyslnie.");
            }
        }
#if BURSZTYN_DEBUG_NET_SELFTEST
        wykonaj_selftest_sieci();
#endif
    } else {
        wypisz_log("[SIEC] Brak zarejestrowanej karty sieciowej.");
    }

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

    Storage::Partition boot_partition{};
    if(instalator_system_zainstalowany()){
        bool found=false;
        for(uint32_t i=0;blokowy_znajdz_bsp(i,&boot_partition);++i){
            psf_ustaw_partycje(&boot_partition);
            inicjalizuj_psf(reinterpret_cast<void*>(ADRES_RAM_DYSKU_PSF),ROZMIAR_RAM_DYSKU_PSF);
            if(psf_weryfikuj_dysk()){found=true;break;}
        }
        if(!found)zatrzymaj_start("Brak kompletnej, poprawnej instalacji BSP2 GPT. Bez automatycznego formatowania.");
    }else inicjalizuj_psf(
        reinterpret_cast<void*>(
            ADRES_RAM_DYSKU_PSF),
        ROZMIAR_RAM_DYSKU_PSF);

    aktualizacja_sprawdz_rozruch(g_multiboot_info_ptr);
    utworz_drabine_katalogow();
    aktualizacja_przygotuj_system_plikow();

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
    if(instalator_tryb()){
        extern uint8_t _binary_instalator_bin_start[],_binary_instalator_bin_end[];
        if(!::zapewnij_katalog("/programy/instalator.cebula")||
           !zapisz_blob_bur(INSTALATOR_PROGRAM,_binary_instalator_bin_start,_binary_instalator_bin_end))
            zatrzymaj_start("Nie mozna przygotowac aplikacji instalatora w RAM.");
    }
    // Restoring the display mode can compose immediately, before WM exists.
    // Resolve the current palette before that first composition.
    motyw_inicjalizuj();
    tapety_inicjalizuj(g_multiboot_info_ptr);
    ekran_inicjalizuj();
    PrezentacjaStartu::zakoncz_inicjalizacje();
    input_set_pointer_ready(true);
    input_pointer_boot_diagnostic();

    if (tryb_testowy) {
        ekran_test_virtio_gpu_modeset(zadano_automatyczne_testy(
            g_multiboot_info_ptr, "bursztyn.test.gpu-capture=1"));
    }

    // Common late activation barrier: memory/IDT/APIC, initialized scheduler,
    // PSF/services, application resources and display bootstrap are complete.
    // No Ring3 GUI process exists yet. Both normal and Installer use this point;
    // IF/dispatch policy stays unchanged until the existing final STI below.
#if BURSZTYNDIAGVIRTIOINPUTINIT
    input_activation_log("kernel phase: bootstrap complete / before Ring3", 1);
    input_activation_log("late activation installer mode", instalator_tryb());
    input_activation_log("late activation scheduler PID", aktualny_pid);
    input_activation_log("late activation scheduler PID0 state", tablica_procesow[0].stan);
    input_activation_log("late activation scheduler dispatch enabled", wielozadaniowosc_aktywna);
#endif
    const uint32_t virtio_input_active = virtio_input_activate_prepared();
    if (virtio_input_active) {
        if (tryb_testowy) virtio_input_stress_test_all();
        else wypisz_log("[VIRTIO-INPUT] Sanity check: OK (devices initialized and registered).");
    }

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

    // Persisted JPEG is decoded by the Ring 3 Tapety process after boot.
    if (tapety_czy_jpeg_do_przywrocenia()) {
        (void)bws_uruchom_program_z_pliku_z_argumentem(
            TAPETY_PROGRAM, PZB_UZYTKOWNIK,
            PRAWO_GUI | PRAWO_PLIKI_CZYTAJ, false, "--restore");
    }

    if (zadano_test_stack_guard(g_multiboot_info_ptr)) {
        wypisz_log("\n[TEST] Wywolanie kontrolowanego testu naruszenia guard page stosu jadra...\n");
        stos_wykonaj_test_przepelnienia(0);
    }

    /* Zwykly start systemu uruchamia tylko Menedzera Okien. Test jest
       aplikacja developerska uruchamiana recznie. Osobny obraz regresyjny
       zadaje jawny tryb automatyczny w wierszu polecen Multiboot2. */
    if (zadano_automatyczne_testy(g_multiboot_info_ptr)) {
        aktualizacja_samotest_automatyczny();
        (void)bws_uruchom_program_z_pliku_z_argumentem(
            "/programy/test.cebula/test.bur",
            PZB_UZYTKOWNIK,
            PRAWO_GUI | PRAWO_URUCHOM_PROGRAM | PRAWO_PLIKI_CZYTAJ,
            false,
            "--automated");
    }

    if(instalator_tryb()){
        // Scheduler jeszcze nieaktywny: pierwszy wolny PID jest przydzielany tylko temu startowi.
        int pid=1;while(pid<MAKS_PROCESOW&&tablica_procesow[pid].stan!=PROCES_PUSTY)++pid;
        if(pid==MAKS_PROCESOW||!bws_uruchom_program_z_pliku(INSTALATOR_PROGRAM,PZB_ZAUFANE,PRAWO_GUI|PRAWO_SYSTEM_CONFIG,false))
            zatrzymaj_start("Loader instalatora nie powiodl sie.");
        instalator_autoryzuj(pid);
        klog_serial("[INSTALL] Installer mode ready; disk sector writes before GUI = 0 (enforced live RAM filesystem).");
    }

    /*
     * Flaga musi zostac ustawiona PRZED STI.
     * Pierwsze przerwanie timera moze od razu wykonac PrzelaczKontekst().
     */
    t_kernel_ready_us = hpet_dostepny() ? czas_monotoniczny_us() : 0;

    wielozadaniowosc_aktywna = true;

    wypisz_log(
        "[SCHED] Wielozadaniowosc Round-Robin aktywna.");

    wypisz_log(
        "--------------------------------------------------");
    wypisz_log(
        "[KERNEL] Jadro i scheduler gotowe.");

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
        input_service_all();
        virtio_input_obsluz();
        ekran_obsluz_timeout();
        zablokuj_ekran();
        skladacz_obrazu_obsluz_dirty();
        odblokuj_ekran();
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
