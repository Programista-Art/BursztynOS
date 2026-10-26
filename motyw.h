#include "kolor_alfa.h"
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define MOTYW_ABI_WERSJA 2
#define BURSZTYN_MOTYW_ABI_WERSJA 2
#define MOTYW_MAX_DLUGOSC_NAZWY 32
#define MOTYW_MAX_DLUGOSC_ID 32
#define MOTYW_SCIEZKA_KATALOG "/system/motywy"
#define MOTYW_SCIEZKA_KONFIG  "/system/konfig/motyw.cfg"

enum MotywKolorRola : uint32_t {
    MOTYW_KOLOR_PULPIT = 0,
    MOTYW_KOLOR_OKNOTLO = 1,
    MOTYW_KOLOR_PANEL = 2,
    MOTYW_KOLOR_PODNIESIONY = 3,
    MOTYW_KOLOR_RAMKA = 4,
    MOTYW_KOLOR_TEKST = 5,
    MOTYW_KOLOR_TEKST_DRUGI = 6,
    MOTYW_KOLOR_WYLACZONY = 7,
    MOTYW_KOLOR_AKCENT = 8,
    MOTYW_KOLOR_AKCENT_NAJECHANY = 9,
    MOTYW_KOLOR_AKCENT_AKTYWNY = 10,
    MOTYW_KOLOR_BLAD = 11,
    MOTYW_KOLOR_SUKCES = 12,
    MOTYW_KOLOR_OSTRZEZENIE = 13,
    MOTYW_KOLOR_INFORMACJA = 14,
    MOTYW_KOLOR_POLETEKSTOWE = 15,
    MOTYW_KOLOR_ZAZNACZENIE = 16,
    MOTYW_KOLOR_PRZYCISK = 17,
    MOTYW_KOLOR_PRZYCISK_NAJECHANY = 18,
    MOTYW_KOLOR_TEKST_AKCENTU = 19,
    MOTYW_KOLOR_LICZBA = 20, // number of stored v2 palette entries
    // Derived central roles: keep the v2 wire structure unchanged.
    MOTYW_KOLOR_PULPIT_ZAZNACZENIE = 20,
    MOTYW_KOLOR_PULPIT_RAMKA = 21,
    MOTYW_KOLOR_PULPIT_TEKST = 22,
    MOTYW_KOLOR_PULPIT_TEKST_ZAZNACZONY = 23,
    MOTYW_KOLOR_ROL_LICZBA = 24
};

// Aliases ról kolorystycznych
inline constexpr MotywKolorRola MOTYW_KOLOR_OKNO = MOTYW_KOLOR_OKNOTLO;
inline constexpr MotywKolorRola MOTYW_KOLOR_OKNO_TLO = MOTYW_KOLOR_OKNOTLO;
inline constexpr MotywKolorRola MOTYW_KOLOR_TLO = MOTYW_KOLOR_PULPIT;
inline constexpr MotywKolorRola MOTYW_KOLOR_PANEL_PODNIESIONY = MOTYW_KOLOR_PODNIESIONY;
inline constexpr MotywKolorRola MOTYW_KOLOR_TEKST_DRUGORZEDNY = MOTYW_KOLOR_TEKST_DRUGI;
inline constexpr MotywKolorRola MOTYW_KOLOR_TEKST_WYLACZONY = MOTYW_KOLOR_WYLACZONY;
inline constexpr MotywKolorRola MOTYW_KOLOR_AKCENT_HOVER = MOTYW_KOLOR_AKCENT_NAJECHANY;
inline constexpr MotywKolorRola MOTYW_KOLOR_PRZYCISK_HOVER = MOTYW_KOLOR_PRZYCISK_NAJECHANY;
inline constexpr MotywKolorRola MOTYW_KOLOR_POLE_TEKSTOWE = MOTYW_KOLOR_POLETEKSTOWE;

enum MotywElement : uint32_t {
    MOTYW_ROG_OKNO = 0,
    MOTYW_ROG_PANEL = 1,
    MOTYW_ROG_PRZYCISK = 2,
    MOTYW_ROG_POLE = 3,
    MOTYW_ROG_MENU = 4,
    MOTYW_ROG_PODPOWIEDZ = 5,
    MOTYW_ROG_LICZBA = 6
};

// Aliases gramatyczne i czytelnościowe
inline constexpr MotywElement MOTYW_ROG_OKNA = MOTYW_ROG_OKNO;
inline constexpr MotywElement MOTYW_ROG_PANELU = MOTYW_ROG_PANEL;
inline constexpr MotywElement MOTYW_ROG_PRZYCISKU = MOTYW_ROG_PRZYCISK;
inline constexpr MotywElement MOTYW_ROG_POLA = MOTYW_ROG_POLE;
inline constexpr MotywElement MOTYW_ROG_PODPOWIEDZI = MOTYW_ROG_PODPOWIEDZ;
inline constexpr MotywElement MOTYW_ROG_TOOLTIP = MOTYW_ROG_PODPOWIEDZ;

enum MotywPolecenie : uint32_t {
    MOTYW_CMD_BRAK = 0,
    MOTYW_CMD_USTAW = 1,
    MOTYW_CMD_PODGLAD = 2,
    MOTYW_CMD_ZATWIERDZ = 3,
    MOTYW_CMD_PRZYWROC = 4,
    MOTYW_CMD_LISTUJ = 5
};

// Struktura ABI v1 (dla kompatybilności wstecznej odczytu BWS 86)
struct BursztynMotywV1 {
    uint32_t wersja;
    uint32_t pulpit;
    uint32_t oknotlo;
    uint32_t panel;
    uint32_t podniesiony;
    uint32_t ramka;
    uint32_t tekst;
    uint32_t tekstdrugorzedny;
    uint32_t wylaczony;
    uint32_t akcent;
    uint32_t akcentnajechany;
    uint32_t akcentaktywny;
    uint32_t blad;
    uint32_t sukces;
    uint32_t ostrzezenie;
    uint32_t informacja;

    uint16_t rogokna;
    uint16_t rogpanelu;
    uint16_t rogprzycisku;
    uint16_t rogpola;
    uint16_t rogmenu;
    uint16_t rogpodpowiedzi;

    uint8_t gruboscramki;
    uint8_t cienokna;
    uint8_t rezerwa[2];

    char nazwa[MOTYW_MAX_DLUGOSC_NAZWY];
    char id[MOTYW_MAX_DLUGOSC_ID];
};
static_assert(sizeof(BursztynMotywV1) == 144, "BursztynMotywV1 ABI musi miec 144 bajty");

// Struktura ABI v2 (172 bajty)
struct BursztynMotyw {
    uint32_t wersja;            // 2
    uint32_t rozmiar_struktury; // 172

    // 20 kolorów ARGB / 0x00RRGGBB (80 B)
    uint32_t pulpit;
    uint32_t oknotlo;
    uint32_t panel;
    uint32_t podniesiony;
    uint32_t ramka;
    uint32_t tekst;
    uint32_t tekstdrugorzedny;
    uint32_t wylaczony;
    uint32_t akcent;
    uint32_t akcentnajechany;
    uint32_t akcentaktywny;
    uint32_t blad;
    uint32_t sukces;
    uint32_t ostrzezenie;
    uint32_t informacja;
    uint32_t poletekstowe;
    uint32_t zaznaczenie;
    uint32_t przycisk;
    uint32_t przycisknajechany;
    uint32_t tekstakcentu;

    // 6 promieni zaokraglen w pikselach (12 B)
    uint16_t rogokna;
    uint16_t rogpanelu;
    uint16_t rogprzycisku;
    uint16_t rogpola;
    uint16_t rogmenu;
    uint16_t rogpodpowiedzi;

    // Parametry obramowania i cienia (2 B + 6 B rezerwy)
    uint8_t gruboscramki;
    uint8_t cienokna;
    uint8_t rezerwa[6];

    char nazwa[MOTYW_MAX_DLUGOSC_NAZWY];
    char id[MOTYW_MAX_DLUGOSC_ID];
};

static_assert(sizeof(BursztynMotyw) == 172, "BursztynMotyw ABI v2 musi miec dokladnie 172 bajty");

inline uint32_t motyw_mieszaj(uint32_t base, uint32_t overlay, unsigned weight) {
    uint32_t result=0;
    for(unsigned shift=0;shift<24;shift+=8)
        result |= (((base>>shift&255)*(255-weight)+(overlay>>shift&255)*weight)/255)<<shift;
    return result;
}
// The root desktop retains straight alpha until wallpaper composition.
// Other surfaces and the visible-black marker keep their legacy RGB contract.
inline unsigned motyw_jasnosc_rgb(uint32_t rgb) {
    return ((rgb>>16&255)*54+(rgb>>8&255)*183+(rgb&255)*19)>>8;
}
inline uint32_t motyw_pulpit_odcien(const BursztynMotyw& m,bool jasny,unsigned weight) {
    bool panel_jasniejszy=motyw_jasnosc_rgb(m.panel)>motyw_jasnosc_rgb(m.tekst);
    uint32_t surface=(jasny==panel_jasniejszy)?m.panel:m.tekst;
    return motyw_mieszaj(surface,m.ramka,weight);
}
// Wallpaper labels need a bright foreground independent of window text.
// Use the brighter theme surface; lift it toward white to keep a dark theme's
// gray text readable too. The dark selected backdrop supplies its contrast.
inline uint32_t motyw_pulpit_tekst_zaznaczony(const BursztynMotyw& m) {
    uint32_t base=motyw_jasnosc_rgb(m.panel)>motyw_jasnosc_rgb(m.tekst)?m.panel:m.tekst;
    uint32_t out=0;
    for(unsigned shift=0;shift<24;shift+=8)out|=((((base>>shift)&255)+3*255)/4)<<shift;
    return out;
}
inline uint32_t motyw_pulpit_zaznaczenie(const BursztynMotyw& m) {
    return 0xc0000000U | motyw_pulpit_odcien(m,false,24);
}
inline uint32_t motyw_pulpit_ramka(const BursztynMotyw& m) {
    return 0xd0000000U | motyw_mieszaj(m.ramka,m.akcent,160);
}
inline uint32_t motyw_pulpit_skladaj(uint32_t background,uint32_t pixel,bool desktop,
                                    const BursztynMotyw* theme=nullptr) {
    (void)theme;
    if(desktop && pixel && pixel!=0x01000000U && (pixel>>24))
        return kolor_nad(0xff000000U | (background&0xffffffU),pixel)&0xffffffU;
    return pixel & 0x00ffffffU;
}

/* Zwraca obliczony wciecie (inset) od brzegu dla cwiartki kola o promieniu r na wierszu dy */
static inline int motyw_promien_inset(int r, int dy) {
    if (dy < 0 || dy >= r || r <= 0) return 0;
    int term = r - 1 - dy;
    int diff = r * r - term * term;
    if (diff <= 0) return r;
    // Bounded integer square root
    int x = diff;
    int y = (x + 1) / 2;
    while (y < x) {
        x = y;
        y = (x + diff / x) / 2;
    }
    int inset = r - x;
    return (inset < 0) ? 0 : inset;
}

/* Walidacja formatu identyfikatora motywu (wylacznie [a-z0-9_], bez sciezki, kropek, ukosnikow) */
static inline bool motyw_waliduj_id(const char* id) {
    if (!id || id[0] == '\0') return false;
    size_t len = 0;
    while (id[len] != '\0') {
        char c = id[len];
        bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
        if (!ok) return false;
        ++len;
        if (len >= MOTYW_MAX_DLUGOSC_ID) return false;
    }
    return len > 0;
}

/* Domyslny wbudowany motyw zapasowy (Bursztyn Nocny ABI v2) */
inline constexpr BursztynMotyw g_domyslny_motyw_fallback = {
    2,
    172,
    0x00111315U, // pulpit #111315
    0x00191C20U, // oknotlo #191C20
    0x0020242AU, // panel #20242A
    0x00262B31U, // podniesiony #262B31
    0x00343A42U, // ramka #343A42
    0x00F2F3F5U, // tekst #F2F3F5
    0x00A7ADB5U, // tekstdrugorzedny #A7ADB5
    0x006F7680U, // wylaczony #6F7680
    0x00F2A900U, // akcent #F2A900
    0x00FFBC32U, // akcentnajechany #FFBC32
    0x00D99100U, // akcentaktywny #D99100
    0x00E85D5DU, // blad #E85D5D
    0x0051B87AU, // sukces #51B87A
    0x00D97706U, // ostrzezenie #D97706
    0x0058A6E7U, // informacja #58A6E7
    0x0015181CU, // poletekstowe
    0x00314A63U, // zaznaczenie #314A63
    0x00262B31U, // przycisk
    0x00343A42U, // przycisknajechany
    0x00111315U, // tekstakcentu
    10, // rogokna
    8,  // rogpanelu
    7,  // rogprzycisku
    6,  // rogpola
    8,  // rogmenu
    6,  // rogpodpowiedzi
    1,  // gruboscramki
    1,  // cienokna
    {0, 0, 0, 0, 0, 0},
    "Bursztyn Nocny",
    "bursztynnocny"
};

#ifdef __cplusplus
extern "C" {
#endif

/* Parsowanie deklaratywnego pliku .motyw */
bool motyw_parsuj(const char* tekst, size_t dlugosc, BursztynMotyw* out);

/* Walidacja struktury i kontrastu */
bool motyw_waliduj(const BursztynMotyw* m);
bool motyw_waliduj_kontrast(const BursztynMotyw* m);

/* Subsystem jadra */
#if defined(BURSZTYN_JADRO) || defined(BURSZTYN_HOST_TEST)
#include "pzb.h"
#else
struct proces;
typedef struct proces proces_t;
#endif

/* Jednolity predykat autoryzacyjny dla mutacji motywu (PZB <= PZB_ZAUFANE oraz PRAWO_SYSTEM_CONFIG) */
bool motyw_czy_uprawniony_do_mutacji(const proces_t* p);

void motyw_inicjalizuj(void);
const BursztynMotyw* motyw_pobierz_aktywny(void);
bool motyw_ustaw_aktywny(const BursztynMotyw* m, bool trwaly);
bool motyw_zaladuj_z_pliku(const char* id, BursztynMotyw* out);
bool motyw_ustaw_z_pliku(const char* id, bool trwaly);
bool motyw_rozpocznij_podglad(const char* id, int pid, uint64_t czas_teraz);
bool motyw_zatwierdz_podglad(int pid);
bool motyw_przywroc_podglad(int pid);
void motyw_sprawdz_timeout_podgladu(uint64_t czas_teraz);
void motyw_posprzataj_proces(int pid);
void motyw_rozglos_zmiane(void);
uint64_t motyw_bws_pobierz(const proces_t* proces, uint64_t uzytkownik_bufor);
uint64_t motyw_bws_polecenie(const proces_t* proces, uint32_t polecenie, uint64_t arg1, uint64_t arg2);
void motyw_zainstaluj_domyslne_pliki(void);

#ifdef __cplusplus
}
#endif
