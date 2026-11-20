#pragma once
/*
 * Bursztyn OS - ABI Bursztynowego Zasobnika (obszar stanu systemu przy zegarze paska zadan).
 * Opis: docs/bursztynowy-zasobnik.md.
 *
 * BWS 107 BWS_ZASOBNIK_STAN      arg1 = bufor Ring 3, arg2 = rozmiar (>= sizeof(BwsZasobnikStanV1)), arg3 = BWS_ZASOBNIK_V1
 *   Migawka stanu: dzwiek (rzeczywisty backend glosnosci), siec (rejestr NetworkDevice + konfiguracja IPv4 stosu),
 *   nosniki wymienne (rejestr blokowy), bateria (flaga braku obslugi), liczniki powiadomien.
 * BWS 108 BWS_ZASOBNIK_POLECENIE arg1 = BWS_ZAS_POL_*, arg2 = wartosc / id powiadomienia
 *   Glosnosc, wyciszenie, odczytanie/usuniecie/wyczyszczenie powiadomien.
 * BWS 109 BWS_POWIADOMIENIA       arg1 = BWS_POW_POL_*, arg2 = bufor Ring 3, arg3 = rozmiar
 *   POBIERZ: lista powiadomien (tylko Menedzer Okien). ZGLOS: powiadomienie aplikacji (PRAWO_GUI, limit czestosci,
 *   nadawca ustalany przez jadro z pliku programu - aplikacja nie moze podac sie za system).
 *
 * BWS 107, 108 i POBIERZ: wylacznie Menedzer Okien - PRAWO_SYSTEM_CONFIG + PZB <= PZB_ZAUFANE ORAZ sciezka programu
 * Menedzera Okien (jak BWS 56 bez wejscia; Ustawienia i Aktualizacje maja te same prawa, ale nie te sciezke).
 *
 * Kazde wywolanie: znana wersja, rozmiar bufora >= struktury, caly zakres sprawdzony (zakres + mapowanie) przed
 * jedna kopia struktury zbudowanej i wyzerowanej w jadrze. Przy bledzie bufor nie jest zmieniany. Zadnych wskaznikow
 * jadra, adresow fizycznych/DMA ani surowych struktur sterownikow - tylko liczby, flagi i nazwy (ASCII/UTF-8, NUL).
 */
#include <stdint.h>
#include <stddef.h>
#include "urzadzenia_telemetria.h"

inline constexpr uint64_t BWS_ZASOBNIK_V1 = 0x315A5342ULL;        // "BSZ1"

/* Wyniki BWS 107/108/109 */
enum : uint64_t {
    BWS_ZAS_BLAD = 0,
    BWS_ZAS_OK = 1,
    BWS_ZAS_BRAK_UPRAWNIEN = 2,
    BWS_ZAS_ZLY_BUFOR = 3,
    BWS_ZAS_ZLA_WERSJA = 4,
    BWS_ZAS_NIEOBSLUGIWANE = 5,    // backend nie istnieje albo nie umie (np. brak regulacji glosnosci)
    BWS_ZAS_ZAJETE = 6,            // sprzet chwilowo zajety - stan NIE zostal zmieniony
    BWS_ZAS_LIMIT = 7,             // limit czestosci / kolejka pelna (powiadomienia aplikacji)
    BWS_ZAS_ZLE_ARGUMENTY = 8,
    BWS_ZAS_BRAK = 9,              // brak powiadomienia o tym id
};

/* ---- dzwiek ---- */
inline constexpr uint32_t BWS_DZW_URZADZENIE = 1U << 0;   // kontroler HDA i trasa wyjscia gotowe
inline constexpr uint32_t BWS_DZW_REGULACJA = 1U << 1;    // poziom 0..100 ma rzeczywisty skutek
inline constexpr uint32_t BWS_DZW_WYCISZANIE = 1U << 2;   // wyciszenie ma rzeczywisty skutek
inline constexpr uint32_t BWS_DZW_WYCISZONY = 1U << 3;
inline constexpr uint32_t BWS_DZW_ODTWARZA = 1U << 4;     // strumien wyjsciowy aktywny (ton/test)
inline constexpr uint32_t BWS_DZW_BACKEND_BRAK = 0U, BWS_DZW_BACKEND_WZMACNIACZ = 1U, BWS_DZW_BACKEND_PCM = 2U;

struct BwsZasDzwiekV1 {
    uint32_t flagi;
    uint32_t backend;          // BWS_DZW_BACKEND_*
    uint32_t glosnosc;         // 0..100 (znaczaca z BWS_DZW_REGULACJA)
    uint32_t generacja;        // rosnie przy kazdej zmianie poziomu/wyciszenia (synchronizacja widokow)
    uint16_t pci_vendor, pci_device;
    uint8_t kodek;             // adres kodeka (CAd)
    uint8_t wezel;             // NID wzmacniacza wyjsciowego (0 = brak)
    uint8_t kroki;             // liczba krokow wzmacniacza (Amp Caps NumSteps)
    uint8_t zarezerwowane;
    char urzadzenie[32];       // np. "Intel HDA (QEMU)"; pusty bez urzadzenia
};
static_assert(sizeof(BwsZasDzwiekV1) == 56, "ABI zasobnika: dzwiek");

/* ---- siec ---- */
inline constexpr uint32_t BWS_ZSIEC_INTERFEJS = 1U << 0;   // interfejs[] opisuje zarejestrowane urzadzenie
inline constexpr uint32_t BWS_ZSIEC_IPV4 = 1U << 1;        // stos ma konfiguracje IPv4 (DHCP ACK)
inline constexpr uint32_t BWS_ZSIEC_BRAMA = 1U << 2;       // brama != 0.0.0.0
inline constexpr uint32_t BWS_ZSIEC_DNS = 1U << 3;         // serwer DNS skonfigurowany
inline constexpr uint32_t BWS_ZSIEC_DHCP = 1U << 4;        // konfiguracja pochodzi z DHCP

struct BwsZasSiecV1 {
    uint32_t flagi;
    uint32_t liczba_interfejsow;   // wszystkie zarejestrowane karty
    uint8_t ip[4], maska[4], brama[4], dns[4];
    uint64_t czas_ns;              // zegar monotoniczny migawki (0 bez HPET): tempo z roznicy migawek
    BwsSiecInfoV1 interfejs;       // glowny interfejs stosu (inaczej pierwszy) - pola jak BWS 105
};
static_assert(sizeof(BwsZasSiecV1) == 24 + 8 + sizeof(BwsSiecInfoV1), "ABI zasobnika: siec");

/* ---- nosniki ---- */
inline constexpr uint32_t BWS_ZAS_MAKS_NOSNIKOW = 4U;
inline constexpr uint32_t BWS_NOS_WYMIENNY = 1U << 0;
inline constexpr uint32_t BWS_NOS_TYLKO_ODCZYT = 1U << 1;
inline constexpr uint32_t BWS_NOS_SYSTEMOWY = 1U << 2;
inline constexpr uint32_t BWS_NOS_BEZPIECZNE_USUWANIE = 1U << 3;   // jadro umie bezpiecznie odlaczyc (dzis: nigdy)

struct BwsZasNosnikV1 {
    uint32_t id, typ, flagi, rozmiar_sektora;
    uint64_t sektory;
    char model[48];
};
static_assert(sizeof(BwsZasNosnikV1) == 72, "ABI zasobnika: nosnik");

/* ---- flagi glowne ---- */
inline constexpr uint32_t BWS_ZAS_BATERIA_OBSLUGA = 1U << 0;   // jadro umie odczytac baterie (dzis: nigdy - brak AML)
inline constexpr uint32_t BWS_ZAS_NOSNIKI_OBCIETE = 1U << 1;

struct BwsZasobnikStanV1 {
    uint32_t wersja;               // BWS_ZASOBNIK_V1 (dolne 32 bity)
    uint32_t rozmiar;              // sizeof(BwsZasobnikStanV1)
    uint32_t flagi;
    uint32_t liczba_nosnikow;
    uint32_t powiadomienia_liczba, powiadomienia_nieprzeczytane;
    uint64_t powiadomienia_generacja;
    BwsZasDzwiekV1 dzwiek;
    BwsZasSiecV1 siec;
    BwsZasNosnikV1 nosniki[BWS_ZAS_MAKS_NOSNIKOW];
};

/* ---- BWS 108 ---- */
enum : uint64_t {
    BWS_ZAS_POL_GLOSNOSC = 1,               // arg2 = 0..100
    BWS_ZAS_POL_WYCISZENIE = 2,             // arg2 = 0 / 1
    BWS_ZAS_POL_PRZECZYTANE = 3,            // arg2 = id
    BWS_ZAS_POL_USUN = 4,                   // arg2 = id
    BWS_ZAS_POL_WYCZYSC = 5,
    BWS_ZAS_POL_PRZECZYTANE_WSZYSTKIE = 6,
};

/* ---- powiadomienia (BWS 109) ---- */
inline constexpr uint32_t BWS_POW_MAKS = 16U;
inline constexpr uint32_t BWS_POW_TYTUL = 64U, BWS_POW_TRESC = 160U, BWS_POW_NADAWCA = 32U;
inline constexpr uint32_t BWS_POW_ZRODLO_SYSTEM = 1U;      // jadro / sterownik: tylko kod jadra moze to ustawic
inline constexpr uint32_t BWS_POW_ZRODLO_APLIKACJA = 2U;   // BWS 109 ZGLOS: nadawca z pliku programu
inline constexpr uint32_t BWS_POW_INFO = 0U, BWS_POW_SUKCES = 1U, BWS_POW_OSTRZEZENIE = 2U, BWS_POW_BLAD = 3U;
inline constexpr uint32_t BWS_POW_PRZECZYTANE = 1U << 0;

enum : uint64_t { BWS_POW_POL_POBIERZ = 1, BWS_POW_POL_ZGLOS = 2 };

struct BwsPowiadomienieV1 {
    uint64_t id;                   // rosnacy, nigdy 0, nie uzywany ponownie w obrebie uruchomienia
    uint64_t czas_ms;              // zegar monotoniczny jadra
    uint32_t zrodlo;               // BWS_POW_ZRODLO_*
    uint32_t poziom;               // BWS_POW_INFO..BLAD
    uint32_t flagi;
    int32_t pid;                   // nadawca (APLIKACJA), 0 dla systemu
    char nadawca[BWS_POW_NADAWCA]; // "System" albo nazwa programu ustalona przez jadro
    char tytul[BWS_POW_TYTUL];
    char tresc[BWS_POW_TRESC];
};
static_assert(sizeof(BwsPowiadomienieV1) == 24 + 8 + 32 + 64 + 160, "ABI powiadomienia");

struct BwsPowiadomieniaListaV1 {
    uint32_t wersja, rozmiar;
    uint32_t liczba, nieprzeczytane;
    uint64_t generacja;
    BwsPowiadomienieV1 wpisy[BWS_POW_MAKS];   // od najnowszego
};

/* ZGLOS: aplikacja podaje tylko poziom (INFO/SUKCES/OSTRZEZENIE - BLAD zarezerwowany dla systemu), tytul i tresc. */
struct BwsPowiadomienieZgloszenieV1 {
    uint32_t wersja, rozmiar;      // BWS_ZASOBNIK_V1, sizeof
    uint32_t poziom, zarezerwowane;
    char tytul[BWS_POW_TYTUL];
    char tresc[BWS_POW_TRESC];
};
static_assert(sizeof(BwsPowiadomienieZgloszenieV1) == 16 + 64 + 160, "ABI zgloszenia powiadomienia");
