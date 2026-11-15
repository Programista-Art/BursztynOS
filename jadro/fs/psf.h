/*
 * Bursztyn OS - Polski System Plikow BSP2 / PSF
 *
 * Publiczny format dyskowy i API systemu plikow.
 *
 * WAZNE:
 * Struktury ponizej sa zapisywane bezposrednio na nosniku. Ich rozmiary,
 * kolejnosc pol i offsety sa czescia ABI formatu BSP2. Nie wolno zmieniac
 * ich bez jednoczesnego:
 *
 *   1. podniesienia wersji formatu,
 *   2. aktualizacji psf.cpp,
 *   3. przygotowania migratora/starego czytnika.
 *
 * Aktualny format:
 *   - blok:            4096 bajtow,
 *   - superblok:         44 bajty,
 *   - wezel indeksowy: 4096 bajtow,
 *   - wpis katalogowy:   64 bajty,
 *   - nazwa: maks. 55 bajtow danych + '\0',
 *   - wskazniki blokow: 64-bitowe.
 *
 * Nazwa "BSP2" pozostaje zachowana dla zgodnosci z obecnymi nosnikami.
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "storage_format.h"
#include "psf_bootstrap_status.h"
#include "plik_status.h"

void psf_ustaw_partycje(const Storage::Partition* partition);
#ifdef __cplusplus
extern "C" {
#endif
void psf_ustaw_aktywny_slot(uint8_t slot);
uint8_t psf_pobierz_aktywny_slot(void);
void psf_wlacz_translacje_slotow(bool wlaczone);
#ifdef __cplusplus
}
#endif
bool psf_formatuj_instalacje(const Storage::Partition& partition);
bool psf_formatuj_instalacje_start(const Storage::Partition& partition,uint64_t* razem);
bool psf_formatuj_instalacje_step(uint64_t* wykonane,bool* gotowe);
bool psf_weryfikuj_dysk();
bool psf_weryfikuj_dysk_start(uint64_t* razem);
bool psf_weryfikuj_dysk_step(uint64_t* wykonane,bool* gotowe);
bool psf_znacznik_instalacji(const uint8_t uuid[16],bool complete);

/* =========================================================================
 * 1. STALE FORMATU BSP2
 * ========================================================================= */

/*
 * Jeden blok systemu plikow ma 4 KiB.
 */
#define PSF_ROZMIAR_BLOKU 4096U

/*
 * Pole nazwa[] ma 56 bajtow.
 *
 * Poniewaz nazwa jest przechowywana jako NUL-terminated string,
 * maksymalna uzyteczna dlugosc nazwy wynosi 55 bajtow.
 */
#define PSF_MAX_NAZWA 56U

/*
 * 490 bezposrednich wskaznikow * 4096 B =
 * 2 007 040 bajtow (~1.914 MiB) danych pliku bez blokow posrednich.
 *
 * Bloki posrednie sa zarezerwowane w formacie, ale obecny psf.cpp
 * celowo ich jeszcze nie obsluguje. Obraz z aktywnymi wskaznikami
 * posrednimi jest odrzucany przy walidacji, aby nie utracic danych.
 */
#define PSF_MAX_BLOKOW_W_WEZLE 490U

/*
 * Typy wezlow.
 */
#define TYP_WOLNY    0U
#define TYP_PLIK     1U
#define TYP_KATALOG  2U

/*
 * Specjalna wartosc oznaczajaca brak przypisanego bloku danych.
 */
#define BARK_BLOKU UINT64_C(0xFFFFFFFFFFFFFFFF)

/*
 * Sygnatura aktualnej wersji systemu plikow.
 */
#define PSF_SYGNATURA_0 'B'
#define PSF_SYGNATURA_1 'S'
#define PSF_SYGNATURA_2 'P'
#define PSF_SYGNATURA_3 '2'

#define PSF_ROZMIAR_SYGNATURY 4U

/* Wersjonowane uzycie dwoch historycznie zarezerwowanych pol inode.
 * Uklad inode i sygnatura BSP2 pozostaja bez zmian. Wartosc bez tego
 * znacznika jest traktowana jak stary węzeł bez metadanych. */
#define PSF_META_MAGIC_MASK UINT64_C(0xFFFFFFFF00000000)
#define PSF_META_MAGIC      UINT64_C(0x5053464D00000000)
#define PSF_META_WERSJA_MASK UINT64_C(0x00000000FF000000)
#define PSF_META_WERSJA_1    UINT64_C(0x0000000001000000)
#define PSF_META_WERSJA_2    UINT64_C(0x0000000002000000)
#define PSF_META_CZAS_WAZNY  UINT64_C(0x0000000000000100)
#define PSF_META_PZB_WAZNY   UINT64_C(0x0000000000000200)
#define PSF_META_PZB_MASK    UINT64_C(0x0000000000000007)

/*
 * Rozmiar danych osiagalny przez obecne wskazniki bezposrednie.
 */
#define PSF_MAKS_ROZMIAR_PLIKU_BEZPOSREDNI \
    (UINT64_C(PSF_MAX_BLOKOW_W_WEZLE) * UINT64_C(PSF_ROZMIAR_BLOKU))

#define PSF_PTRS_PER_BLOCK (PSF_ROZMIAR_BLOKU / sizeof(uint64_t))
#define PSF_MAKS_ROZMIAR_PLIKU_DUZY \
    ( ( (uint64_t)PSF_MAX_BLOKOW_W_WEZLE + (uint64_t)PSF_PTRS_PER_BLOCK + \
       (uint64_t)PSF_PTRS_PER_BLOCK * (uint64_t)PSF_PTRS_PER_BLOCK) * (uint64_t)PSF_ROZMIAR_BLOKU )
#define PSF_MAKS_ROZMIAR_PLIKU PSF_MAKS_ROZMIAR_PLIKU_DUZY
#define PSF_MAX_BLOCKS_DUZY ((uint64_t)PSF_MAX_BLOKOW_W_WEZLE + (uint64_t)PSF_PTRS_PER_BLOCK + (uint64_t)PSF_PTRS_PER_BLOCK*(uint64_t)PSF_PTRS_PER_BLOCK)

/* =========================================================================
 * 2. SUPERBLOK BSP2
 * ========================================================================= */

/*
 * Superblok znajduje sie na poczatku bloku 0.
 *
 * Format na dysku:
 *
 *   offset  rozmiar  pole
 *   0x00       4     sygnatura = "BSP2"
 *   0x04       8     calkowity_rozmiar
 *   0x0C       8     ilosc_blokow
 *   0x14       8     id_korzenia
 *   0x1C       8     start_wezlow
 *   0x24       8     start_danych
 *
 * Razem: 44 bajty.
 *
 * calkowity_rozmiar jest 64-bitowy jako element formatu BSP2.
 * Aktualne publiczne API inicjalizuj_psf() przyjmuje jednak uint32_t,
 * wiec obecna implementacja ramdysku nie wykorzystuje jeszcze pelnego
 * 64-bitowego zakresu tego pola.
 */
struct superblok {
    char sygnatura[PSF_ROZMIAR_SYGNATURY];

    uint64_t calkowity_rozmiar;
    uint64_t ilosc_blokow;
    uint64_t id_korzenia;
    uint64_t start_wezlow;
    uint64_t start_danych;
} __attribute__((packed));

/* =========================================================================
 * 3. WEZEL INDEKSOWY
 * ========================================================================= */

/*
 * Jeden wezel zajmuje DOKLADNIE jeden blok 4 KiB.
 *
 * Layout:
 *
 *   typ                  - rodzaj wezla,
 *   rozmiar_w_bajtach    - rozmiar pliku lub liczba wpisow katalogu,
 *   czas_utworzenia      - zarezerwowane pod RTC/czas systemowy,
 *   flagi_zabezpieczen   - zarezerwowane pod prawa/PZB,
 *   wskazniki_blokow     - bezposrednie RELATYWNE numery blokow danych,
 *   blok_posredni_1..3   - zarezerwowane pod przyszle duze pliki,
 *   zarezerwowane        - padding ABI do 4096 bajtow.
 *
 * Wskaznik bloku danych nie jest absolutnym numerem bloku systemu plikow.
 * psf.cpp interpretuje go jako:
 *
 *   absolutny_blok = superblok.start_danych + wskaznik
 */
struct wezel_indeksowy {
    uint8_t typ;

    uint64_t rozmiar_w_bajtach;
    uint64_t czas_utworzenia;
    uint64_t flagi_zabezpieczen;

    uint64_t wskazniki_blokow[
        PSF_MAX_BLOKOW_W_WEZLE
    ];

    /*
     * ZAREZERWOWANE W ABI.
     *
     * Obecna implementacja BSP2 ich nie obsluguje.
     * Poprawny aktywny wezel obecnej wersji musi miec tutaj BARK_BLOKU.
     */
    uint64_t blok_posredni_1;
    uint64_t blok_posredni_2;
    uint64_t blok_posredni_3;

    uint8_t zarezerwowane[127];
} __attribute__((packed));

/* =========================================================================
 * 4. WPIS KATALOGOWY
 * ========================================================================= */

/*
 * Wpis katalogowy ma dokladnie 64 bajty:
 *
 *   8 B  - id wezla,
 *  56 B  - nazwa zakonczona '\0'.
 *
 * id_wezla == 0 oznacza pusty wpis katalogowy.
 */
struct wpis_katalogowy {
    uint64_t id_wezla;
    char nazwa[PSF_MAX_NAZWA];
} __attribute__((packed));

struct psf_metadane {
    uint64_t rozmiar;
    uint64_t czas_utworzenia_rtc;
    uint8_t typ;
    uint8_t poziom_pzb;
    uint8_t czas_dostepny;
    uint8_t pzb_dostepny;
};

// Called under the PSF lock: use the supplied metadata, do not call PSF again.
using PsfAutoryzacjaUsuwania = WynikPlikowy (*)(const char*, const psf_metadane&,
                                               const psf_metadane&, void*);
extern "C" WynikPlikowy psf_usun_drzewo(const char*, PsfAutoryzacjaUsuwania, void*);
extern "C" WynikPlikowy psf_usun_wynik(const char*);
extern "C" WynikPlikowy psf_utworz_wynik(const char*, bool katalog, uint8_t pzb);

/* =========================================================================
 * 5. KONTROLA ABI FORMATU DYSKOWEGO
 * ========================================================================= */

#ifdef __cplusplus

static_assert(
    PSF_ROZMIAR_BLOKU == 4096U,
    "BSP2 wymaga blokow 4096 bajtow"
);

static_assert(
    PSF_MAX_NAZWA == 56U,
    "Zmiana PSF_MAX_NAZWA zmienia ABI wpisu katalogowego"
);

static_assert(
    PSF_MAX_BLOKOW_W_WEZLE == 490U,
    "Zmiana liczby wskaznikow zmienia ABI wezla BSP2"
);

static_assert(
    sizeof(superblok) == 44,
    "Superblok BSP2 musi miec dokladnie 44 bajty"
);

static_assert(
    alignof(superblok) == 1,
    "Superblok BSP2 musi pozostac packed"
);

static_assert(
    offsetof(superblok, sygnatura) == 0x00,
    "Nieprawidlowy offset sygnatury superbloku"
);

static_assert(
    offsetof(superblok, calkowity_rozmiar) == 0x04,
    "Nieprawidlowy offset calkowity_rozmiar"
);

static_assert(
    offsetof(superblok, ilosc_blokow) == 0x0C,
    "Nieprawidlowy offset ilosc_blokow"
);

static_assert(
    offsetof(superblok, id_korzenia) == 0x14,
    "Nieprawidlowy offset id_korzenia"
);

static_assert(
    offsetof(superblok, start_wezlow) == 0x1C,
    "Nieprawidlowy offset start_wezlow"
);

static_assert(
    offsetof(superblok, start_danych) == 0x24,
    "Nieprawidlowy offset start_danych"
);

static_assert(
    sizeof(wezel_indeksowy) == PSF_ROZMIAR_BLOKU,
    "Wezel BSP2 musi zajmowac dokladnie jeden blok 4096 bajtow"
);

static_assert(
    alignof(wezel_indeksowy) == 1,
    "Wezel BSP2 musi pozostac packed"
);

static_assert(
    offsetof(wezel_indeksowy, typ) == 0x000,
    "Nieprawidlowy offset typ"
);

static_assert(
    offsetof(wezel_indeksowy, rozmiar_w_bajtach) == 0x001,
    "Nieprawidlowy offset rozmiar_w_bajtach"
);

static_assert(
    offsetof(wezel_indeksowy, czas_utworzenia) == 0x009,
    "Nieprawidlowy offset czas_utworzenia"
);

static_assert(
    offsetof(wezel_indeksowy, flagi_zabezpieczen) == 0x011,
    "Nieprawidlowy offset flagi_zabezpieczen"
);

static_assert(
    offsetof(wezel_indeksowy, wskazniki_blokow) == 0x019,
    "Nieprawidlowy offset wskaznikow bezposrednich"
);

static_assert(
    offsetof(wezel_indeksowy, blok_posredni_1) == 0xF69,
    "Nieprawidlowy offset blok_posredni_1"
);

static_assert(
    offsetof(wezel_indeksowy, blok_posredni_2) == 0xF71,
    "Nieprawidlowy offset blok_posredni_2"
);

static_assert(
    offsetof(wezel_indeksowy, blok_posredni_3) == 0xF79,
    "Nieprawidlowy offset blok_posredni_3"
);

static_assert(
    offsetof(wezel_indeksowy, zarezerwowane) == 0xF81,
    "Nieprawidlowy offset pola zarezerwowane"
);

static_assert(
    sizeof(wpis_katalogowy) == 64,
    "Wpis katalogowy BSP2 musi miec dokladnie 64 bajty"
);

static_assert(
    alignof(wpis_katalogowy) == 1,
    "Wpis katalogowy BSP2 musi pozostac packed"
);

static_assert(
    offsetof(wpis_katalogowy, id_wezla) == 0,
    "Nieprawidlowy offset id_wezla"
);

static_assert(
    offsetof(wpis_katalogowy, nazwa) == 8,
    "Nazwa wpisu katalogowego musi zaczynac sie pod offsetem 8"
);

static_assert(
    (PSF_ROZMIAR_BLOKU %
     sizeof(wpis_katalogowy)) == 0,
    "Wpisy katalogowe musza rowno wypelniac blok BSP2"
);

static_assert(
    (PSF_ROZMIAR_BLOKU %
     sizeof(wezel_indeksowy)) == 0,
    "Wezel musi rowno wypelniac blok BSP2"
);

#endif /* __cplusplus */

/* =========================================================================
 * 6. PUBLICZNE API BSP2
 * ========================================================================= */

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Inicjalizuje system plikow w dostarczonym ramdysku.
 *
 * Implementacja:
 *   - probuje zamontowac prawidlowy BSP2 z glownego nosnika blokowego,
 *   - przy braku BSP2 tworzy nowy system,
 *   - przy wykryciu uszkodzonego istniejacego BSP2 nie nadpisuje
 *     automatycznie fizycznego nosnika.
 *
 * UWAGA:
 * Funkcja zwraca void dla zgodnosci z istniejacym kernelem.
 * Docelowo warto zmienic wynik na bool/enum stanu montowania.
 */
void inicjalizuj_psf(
    void* adres_ram_dysku,
    uint32_t rozmiar_w_bajtach
);

/*
 * Tworzenie.
 *
 * false:
 *   - obiekt juz istnieje,
 *   - rodzic nie istnieje,
 *   - nazwa/sciezka jest nieprawidlowa,
 *   - brak wolnego wezla lub bloku katalogowego.
 */
bool utworz_katalog(
    const char* sciezka
);

bool utworz_plik(
    const char* sciezka
);

bool utworz_katalog_z_pzb(const char* sciezka, uint8_t poziom_pzb);
bool utworz_plik_z_pzb(const char* sciezka, uint8_t poziom_pzb);

/*
 * Nadpisuje caly plik.
 *
 * Przy krotszym zapisie niepotrzebne bloki sa zwalniane.
 * Przy dlugosc == 0 plik zostaje wyzerowany/skrocony do zera.
 *
 * Obecna implementacja korzysta tylko ze wskaznikow bezposrednich,
 * wiec rozmiar nie moze przekroczyc PSF_MAKS_ROZMIAR_PLIKU_BEZPOSREDNI.
 */
bool zapisz_do_pliku(
    const char* sciezka,
    const char* dane,
    uint32_t dlugosc
);

bool psf_czy_gotowy();

// Narrow kernel bootstrap path: directory lookup, idempotent create, status.
// Generic creation and listing contracts remain unchanged.
bool psf_przygotuj_katalog_startowy(const char* sciezka, PsfStanStartu* stan);

bool zapisz_fragment_pliku(
    const char* sciezka,
    uint64_t offset,
    const char* dane,
    uint32_t dlugosc
);

/*
 * Czyta maksymalnie max_dlugosc bajtow.
 *
 * Funkcja nie dodaje automatycznie '\0', bo pliki sa binarne.
 */
bool czytaj_z_pliku(
    const char* sciezka,
    char* bufor,
    uint32_t max_dlugosc
);

/* Odczyt fragmentu (pread) – bezpieczny dla dużych plików */
bool czytaj_fragment_pliku(
    const char* sciezka,
    uint64_t offset,
    char* bufor,
    uint32_t dlugosc,
    uint32_t* odczytano
);

/* Atomic, bounded executable snapshot; validates before allocation.
 * No shared temporary storage. A successful result must be freed with kfree. */
struct BurObrazPliku;
struct NaglowekBur;
bool bsp_wczytaj_obraz_bur(const char*, struct BurObrazPliku*, uint64_t,
    const char* (*walidator)(const struct NaglowekBur&, uint64_t));

/*
 * Surowy, ograniczony odczyt pliku wykonywalnego bez interpretacji naglowka.
 *
 * Potrzebny formatom, ktore maja wlasny walidator poza BUR (ELF64). Kontrakt
 * bezpieczenstwa jest identyczny jak powyzej: rozmiar sprawdzany PRZED
 * alokacja, wynik jest prywatna kopia w pamieci jadra, a zwolnienie nalezy
 * do wywolujacego (kfree). Zwraca false i nie alokuje przy kazdym bledzie.
 */
bool bsp_wczytaj_obraz_binarny(const char* sciezka, uint8_t** dane,
    uint64_t* rozmiar, uint64_t maks);

/*
 * Zapisuje tekstowa liste elementow katalogu do bufora.
 *
 * Bufor zawsze jest zakonczony '\0', jezeli max_dlugosc > 0 i argumenty
 * sa poprawne.
 */
bool wylistuj_katalog(
    const char* sciezka,
    char* bufor,
    uint32_t max_dlugosc
);

/*
 * Typowany, ograniczony odczyt jednego wpisu katalogu. Jest przeznaczony
 * dla VFS: w przeciwienstwie do wylistuj_katalog() nie parsuje tekstowego
 * formatu i nie zwraca danych z dysku bez sprawdzenia wezla oraz nazwy.
 */
struct PsfKatalogWpis {
    uint64_t inode;
    uint8_t typ;
    char nazwa[PSF_MAX_NAZWA];
};

enum PsfKatalogWynik : uint8_t {
    PSF_KATALOG_BLAD = 0,
    PSF_KATALOG_KONIEC = 1,
    PSF_KATALOG_WPIS = 2
};

/*
 * `indeks` jest logicznym offsetem wpisu (bez pustych slotow). Zwraca
 * KONIEC dla poprawnego, ale wyczerpanego katalogu i BLAD dla zlego PSF,
 * sciezki lub obiektu niebedacego katalogiem.
 */
enum PsfKatalogWynik psf_pobierz_wpis_katalogu(
    const char* sciezka,
    uint64_t indeks,
    struct PsfKatalogWpis* wpis
);

/*
 * Usuwa plik albo PUSTY katalog.
 *
 * Korzenia "/" nie mozna usunac.
 * Obecna implementacja nie wykonuje rekurencyjnego kasowania katalogow.
 */
bool usun_twor(
    const char* sciezka
);

/*
 * Zwraca rozmiar pliku w bajtach.
 *
 * 0 oznacza zarowno pusty plik, jak i blad/brak pliku - takie zachowanie
 * jest zachowane dla kompatybilnosci z istniejacym API.
 */
uint32_t rozmiar_pliku(
    const char* sciezka
);

/*
 * Zmienia tylko nazwe obiektu w tym samym katalogu.
 *
 * Nie przenosi obiektu pomiedzy katalogami i nie nadpisuje istniejacej
 * nazwy.
 */
bool zmien_nazwe_tworu(
    const char* sciezka,
    const char* nowa_nazwa
);

/* Przenosi do istniejacego folderu, zachowujac nazwe. Nie nadpisuje. */
enum PsfMoveWynik {
    PSF_MOVE_OK, PSF_MOVE_BAD_PATH, PSF_MOVE_MISSING_SOURCE, PSF_MOVE_BAD_DESTINATION,
    PSF_MOVE_ROOT, PSF_MOVE_SELF_DESCENDANT, PSF_MOVE_COLLISION,
    PSF_MOVE_PREPARE_FAILED, PSF_MOVE_COMMIT_FAILED
};
enum PsfMoveWynik psf_przenies_twor_wynik(const char* sciezka, const char* folder_docelowy);
bool przenies_twor(const char* sciezka, const char* folder_docelowy);
#ifdef BURSZTYN_PSF_HOST_TEST
void psf_test_move_failure(unsigned stage);
bool psf_test_lock_held();
#endif

/* Kopia bez nadpisywania. Pliki sa kopiowane blokami 4 KiB, foldery
 * rekurencyjnie z limitem glebokosci; nowe wezly otrzymuja poziom tworzacego. */
bool kopiuj_twor_z_pzb(const char* sciezka, const char* folder_docelowy,
                       uint8_t poziom_pzb);

/* Odczyt rzeczywistych danych inode; stare BSP2 jawnie nie maja czasu/PZB. */
bool pobierz_metadane_tworu(const char* sciezka, psf_metadane* metadane);

/*
 * Zapisuje wszystkie brudne bloki metadanych oraz danych z cache na nosnik blokowy.
 * Zwraca true w przypadku sukcesu. W razie bledu I/O brudne bloki pozostaja
 * w cache do ponownej proby zapisu i funkcja zwraca false.
 */
bool psf_flush_cache();

#ifdef __cplusplus
} /* extern "C" */
#endif

#ifdef __cplusplus
extern "C" {
#endif

bool czy_katalog_istnieje(
    const char* sciezka
);

bool zapewnij_katalog(
    const char* sciezka
);

#ifdef __cplusplus
}
#endif
