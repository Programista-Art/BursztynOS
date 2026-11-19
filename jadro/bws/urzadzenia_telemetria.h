#pragma once
/*
 * Bursztyn OS - ABI telemetrii urzadzen (BWS 105) i licznikow I/O procesow (BWS 106).
 * Opis semantyki kazdego licznika: docs/menedzer-zadan-monitorowanie.md.
 *
 * Oba wywolania sa tylko do odczytu i wymagaja tego samego prawa co BWS 103:
 * PRAWO_ZARZADZANIE_PROCESAMI i PZB <= PZB_ZAUFANE (prawo nadaje wylacznie tabela aplikacji systemowych
 * jadra). Adres MAC i liczniki per proces nie sa wiec widoczne dla zwyklych aplikacji.
 *
 * BWS 105  arg1 = bufor Ring 3, arg2 = rozmiar (>= sizeof(BwsUrzadzeniaOdpowiedz)), arg3 = BWS_URZADZENIA_V1_ABI
 * BWS 106  arg1 = bufor Ring 3, arg2 = rozmiar (>= sizeof(BwsProcesyIoOdpowiedz)),  arg3 = BWS_PROCESY_IO_V1_ABI
 *   Zwracaja BWS_TELEMETRIA_* (te same wartosci co BWS_MIGAWKA_*). Przy bledzie bufor nie jest zmieniany.
 *
 * Zadnych wskaznikow jadra, adresow DMA, zawartosci pakietow ani numerow seryjnych - tylko liczniki,
 * stale identyfikatory i nazwy nadane przez sterownik. Wszystkie liczniki sa skumulowane, 64-bitowe,
 * nasycajace sie (nie zawijaja); tempo liczy aplikacja z roznicy dwoch migawek i czas_ns.
 */
#include <stdint.h>
#include <stddef.h>

inline constexpr uint32_t BWS_URZADZENIA_V1_ABI = 1U;
inline constexpr uint32_t BWS_PROCESY_IO_V1_ABI = 1U;
inline constexpr uint32_t BWS_URZ_MAKS_DYSKOW = 8U;
inline constexpr uint32_t BWS_URZ_MAKS_SIECI = 8U;
inline constexpr uint32_t BWS_URZ_MAKS_GRAFIKI = 1U;
inline constexpr uint32_t BWS_URZ_BRAK = 0xFFFFFFFFU;
inline constexpr uint32_t BWS_IO_MAKS_PROCESOW = 16U;

enum BwsTelemetriaWynik : uint64_t {
    BWS_TELEMETRIA_BLAD = 0,
    BWS_TELEMETRIA_OK = 1,
    BWS_TELEMETRIA_BRAK_UPRAWNIEN = 2,
    BWS_TELEMETRIA_ZLY_BUFOR = 3,
    BWS_TELEMETRIA_ZLA_WERSJA = 4,
};

/* Flagi naglowka BWS 105 */
inline constexpr uint32_t BWS_URZ_ZEGAR = 1U << 0;              // czas_ns z HPET (inaczej 0: tempa niedostepne)
inline constexpr uint32_t BWS_URZ_DYSKI_OBCIETE = 1U << 1;
inline constexpr uint32_t BWS_URZ_SIECI_OBCIETE = 1U << 2;
inline constexpr uint32_t BWS_URZ_PSF_W_RAM = 1U << 3;          // system plikow tylko w RAM (Live): pliki nie trafiaja na dysk
inline constexpr uint32_t BWS_URZ_PSF_NA_DYSKU = 1U << 4;       // PSF z trwalym zapisem na psf_dysk

/* Typ dysku = BlockDeviceType */
inline constexpr uint32_t BWS_DYSK_AHCI = 1U, BWS_DYSK_VIRTIO = 2U, BWS_DYSK_NVME = 3U;

/* Flagi dysku */
inline constexpr uint32_t BWS_DYSK_ONLINE = 1U << 0;            // sterownik gotowy (pojemnosc > 0)
inline constexpr uint32_t BWS_DYSK_TYLKO_ODCZYT = 1U << 1;
inline constexpr uint32_t BWS_DYSK_WYMIENNY = 1U << 2;
inline constexpr uint32_t BWS_DYSK_MODEL_URZADZENIA = 1U << 3;  // model z IDENTIFY urzadzenia (inaczej: brak modelu)
inline constexpr uint32_t BWS_DYSK_SYSTEMOWY = 1U << 4;         // na nim zamontowany jest trwaly PSF

struct BwsDyskInfoV1 {
    uint32_t id;                   // staly w obrebie uruchomienia (kolejnosc rejestracji)
    uint32_t typ;                  // BWS_DYSK_*
    uint32_t flagi;
    uint32_t rozmiar_sektora;      // logiczny, bajty
    uint64_t sektory;              // pojemnosc zgloszona przez sterownik
    uint64_t bajty_odczytane, bajty_zapisane;
    uint64_t operacje_odczytu, operacje_zapisu;     // polecenia zakonczone sukcesem
    uint64_t bledy_odczytu, bledy_zapisu;           // polecenia zakonczone bledem sterownika
    uint64_t czas_odczytu_ns, czas_zapisu_ns;       // suma czasu polecen z pomiarem
    uint64_t operacje_z_czasem;                     // polecenia (odczyt+zapis, udane i nie) z pomiarem czasu
    uint64_t oprozniania, bledy_oprozniania;        // FLUSH
    char model[48];                // NUL; pusty, gdy brak BWS_DYSK_MODEL_URZADZENIA
};

/* Typ sieci = NetDeviceType */
inline constexpr uint32_t BWS_SIEC_E1000 = 1U, BWS_SIEC_VIRTIO = 2U;
/* Flagi sieci */
inline constexpr uint32_t BWS_SIEC_AKTYWNA = 1U << 0;           // zarejestrowana i aktywna w rejestrze
inline constexpr uint32_t BWS_SIEC_LACZE_ZNANE = 1U << 1;       // sterownik raportuje stan lacza
inline constexpr uint32_t BWS_SIEC_LACZE = 1U << 2;             // lacze aktywne (gdy LACZE_ZNANE)
inline constexpr uint32_t BWS_SIEC_GLOWNA = 1U << 3;            // interfejs uzywany przez stos IP
inline constexpr uint32_t BWS_SIEC_MAC = 1U << 4;               // mac[] poprawny
inline constexpr uint32_t BWS_SIEC_PREDKOSC = 1U << 5;          // predkosc_mbps raportowana przez sterownik

struct BwsSiecInfoV1 {
    uint32_t id;                   // staly w obrebie uruchomienia (numer rejestracji, nie indeks)
    uint32_t typ;
    uint32_t flagi;
    uint32_t mtu;
    uint8_t mac[6];
    uint8_t zarezerwowane[2];
    uint32_t predkosc_mbps;        // 0 bez BWS_SIEC_PREDKOSC
    uint64_t rx_bajty, rx_pakiety, rx_bledy, rx_odrzucone;
    uint64_t tx_bajty, tx_pakiety, tx_bledy, tx_odrzucone;
    char nazwa[16];                // nazwa interfejsu nadana przez sterownik
};

/* Backend = EKRAN_* (ekran_format.h): 1 VESA, 2 UEFI GOP, 3 Bochs VBE, 4 VirtIO-GPU */
inline constexpr uint32_t BWS_GRAF_KURSOR_SPRZETOWY = 1U << 0;  // kursor jako plaszczyzna urzadzenia
inline constexpr uint32_t BWS_GRAF_PCI = 1U << 1;               // pci_vendor/pci_device poprawne
inline constexpr uint32_t BWS_GRAF_LICZNIKI_VIRTIO = 1U << 2;   // polecenia/transfery/flush VirtIO-GPU
inline constexpr uint32_t BWS_GRAF_BLAD_TRWALY = 1U << 3;       // sterownik zglosil trwala awarie
inline constexpr uint32_t BWS_GRAF_ZEGAR = 1U << 4;             // czasy kompozytora mierzone HPET
/* Brak flagi "obciazenie GPU": zaden backend nie ma licznika wykorzystania sprzetu. */

struct BwsGrafikaInfoV1 {
    uint32_t backend;
    uint32_t flagi;
    uint32_t szerokosc, wysokosc, bpp, pitch;
    uint32_t format_virtio;        // VIRTIO_GPU_FORMAT_* (0 = nie dotyczy)
    uint16_t pci_vendor, pci_device;
    uint64_t klatki;               // klatki kompozytora (prezentacje sceny)
    uint64_t pelne_klatki;         // klatki skladane w calosci
    uint64_t czas_skladania_ns;    // programowe skladanie (CPU, czas scienny)
    uint64_t czas_prezentacji_ns;  // przekazanie klatki do urzadzenia
    uint64_t piksele_skladane;
    uint64_t kopie_bajty, czas_kopii_ns;          // kopie backbuffer -> framebuffer (GOP/VESA/Bochs)
    uint64_t polecenia_gpu, czas_oczekiwania_gpu_ns, bledy_gpu, spoznione_gpu;
    uint64_t transfery, piksele_transferu, flush, piksele_flush;  // VirtIO-GPU TRANSFER_TO_HOST_2D / RESOURCE_FLUSH
    uint64_t prezentacje_kursora;
    uint64_t pamiec_warstw_bajtow;  // bufory warstw okien kompozytora
    uint64_t pamiec_buforow_bajtow; // bufory grafiki (backbuffer, tapeta) zaalokowane przez jadro
};

struct BwsUrzadzeniaNaglowek {
    uint32_t wersja;               // BWS_URZADZENIA_V1_ABI
    uint32_t rozmiar_naglowka;
    uint32_t rozmiar_dysku, rozmiar_sieci, rozmiar_grafiki;
    uint32_t liczba_dyskow, liczba_sieci, liczba_grafiki;
    uint32_t wszystkich_dyskow, wszystkich_sieci;   // przed obcieciem
    uint32_t flagi;
    uint32_t psf_dysk;             // id dysku z trwalym PSF albo BWS_URZ_BRAK
    uint64_t czas_ns;              // zegar monotoniczny chwili migawki (0 bez HPET)
};

struct BwsUrzadzeniaOdpowiedz {
    BwsUrzadzeniaNaglowek naglowek;
    BwsDyskInfoV1 dyski[BWS_URZ_MAKS_DYSKOW];
    BwsSiecInfoV1 sieci[BWS_URZ_MAKS_SIECI];
    BwsGrafikaInfoV1 grafika[BWS_URZ_MAKS_GRAFIKI];
};

static_assert(sizeof(BwsUrzadzeniaNaglowek) == 56, "ABI BWS 105: naglowek");
static_assert(sizeof(BwsDyskInfoV1) == 160, "ABI BWS 105: dysk");
static_assert(sizeof(BwsSiecInfoV1) == 112, "ABI BWS 105: siec");
static_assert(sizeof(BwsGrafikaInfoV1) == 176, "ABI BWS 105: grafika");
static_assert(offsetof(BwsUrzadzeniaOdpowiedz, dyski) == 56, "ABI BWS 105: uklad");

/* ---------------------------------------------------------------- BWS 106 */
inline constexpr uint32_t BWS_IO_ZEGAR = 1U << 0;
/* Flagi wpisu procesu */
inline constexpr uint32_t BWS_IO_PLIKI = 1U << 0;   // liczniki plikow aktywne dla tej generacji
inline constexpr uint32_t BWS_IO_SIEC = 1U << 1;    // liczniki sieci aktywne dla tej generacji

/* Liczniki skumulowane jednej generacji procesu (PID, generacja). */
struct BwsProcesIoV1 {
    uint32_t pid;
    uint32_t flagi;
    uint64_t generacja;
    uint64_t pliki_odczyt_bajtow;   // logiczne bajty przekazane przez wywolania plikowe BWS (5, 59)
    uint64_t pliki_zapis_bajtow;    // logiczne bajty przyjete przez wywolania plikowe BWS (3, 98)
    uint64_t siec_wyslane_bajtow;   // dane TCP (payload segmentow) sesji, ktorej wlascicielem byl proces
    uint64_t siec_odebrane_bajtow;  // dane TCP przyjete w kolejnosci dla tej sesji
};

struct BwsProcesyIoNaglowek {
    uint32_t wersja;
    uint32_t rozmiar_naglowka;
    uint32_t rozmiar_wpisu;
    uint32_t liczba_wpisow;
    uint32_t flagi;
    uint32_t zarezerwowane;
    uint64_t czas_ns;
    /* Ruch bez wlasciciela w procesie uzytkownika - liczony, ale nieprzypisany: */
    uint64_t siec_jadro_wyslane, siec_jadro_odebrane;               // sesja otwarta przez jadro (PID 0)
    uint64_t siec_zakonczone_wyslane, siec_zakonczone_odebrane;     // pakiety po zakonczeniu wlasciciela
    uint64_t pliki_zakonczone_odczyt, pliki_zakonczone_zapis;       // (rezerwa: wywolania plikowe sa synchroniczne)
};

struct BwsProcesyIoOdpowiedz {
    BwsProcesyIoNaglowek naglowek;
    BwsProcesIoV1 wpisy[BWS_IO_MAKS_PROCESOW];
};

static_assert(sizeof(BwsProcesyIoNaglowek) == 80, "ABI BWS 106: naglowek");
static_assert(sizeof(BwsProcesIoV1) == 48, "ABI BWS 106: wpis");

namespace UrzadzeniaTelemetria {

/* Bufor musi pomiescic cala odpowiedz (stala wielkosc); bez przepelnien. */
inline bool bufor_poprawny(uint64_t adres, uint64_t rozmiar, uint64_t wymagany) {
    return adres != 0 && rozmiar >= wymagany;
}

/* Kopia napisu sterownika do pola ABI: zawsze NUL, tylko drukowalne ASCII (bez bajtow sterujacych). */
inline void kopiuj_nazwe(char* cel, size_t pojemnosc, const char* zrodlo, size_t maks_zrodla) {
    if (!cel || !pojemnosc) return;
    size_t n = 0;
    if (zrodlo)
        for (; n + 1 < pojemnosc && n < maks_zrodla && zrodlo[n]; ++n) {
            const char c = zrodlo[n];
            cel[n] = (c >= 0x20 && c < 0x7F) ? c : '?';
        }
    while (n > 0 && cel[n - 1] == ' ') --n;          // model IDENTIFY jest dopelniany spacjami
    for (size_t i = n; i < pojemnosc; ++i) cel[i] = '\0';
}

}  // namespace UrzadzeniaTelemetria
