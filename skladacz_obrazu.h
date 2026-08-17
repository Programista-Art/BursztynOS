/*
 * Bursztyn OS - Skladacz Obrazu
 *
 * Publiczny interfejs kernelowego kompozytora warstw GUI.
 *
 * Model obecnej implementacji:
 *
 *   - jeden slot warstwy przypada na jeden PID,
 *   - PID jest jednoczesnie indeksem tablica_warstw[],
 *   - pojedynczy proces moze miec obecnie najwyzej jedna warstwe,
 *   - bufor warstwy nalezy do skladacza i jest alokowany przez kmalloc(),
 *   - wartosc 0x00000000 oznacza piksel przezroczysty,
 *   - nizszy z_order jest skladany wczesniej,
 *   - przy tym samym z_order tie-breakerem jest PID.
 *
 * WAZNE:
 * pobierz_warstwe() zwraca surowy wskaznik do globalnego slotu. W obecnym
 * jednordzeniowym modelu BWS operacje GUI sa serializowane zewnetrzna
 * blokada ekranu. Przy przyszlym SMP ten interfejs powinien zostac
 * zastapiony bezpieczniejszym snapshotem/refcountem lub uchwytem.
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "pzb.h"

/* =========================================================================
 * 1. LIMIT WARSTW
 * ========================================================================= */

/*
 * Aktualny skladacz przechowuje dokladnie jedna warstwe na slot procesu.
 * Jedynym zrodlem limitu procesow jest pzb.h.
 */
#ifdef __cplusplus

inline constexpr int SKLADACZ_MAKS_WARSTW =
    static_cast<int>(
        PZB_MAKS_PROCESOW
    );

static_assert(
    SKLADACZ_MAKS_WARSTW == 16,
    "Obecny ABI skladacza obrazu zaklada 16 slotow warstw"
);

#endif /* __cplusplus */

/* =========================================================================
 * 2. FORMAT PIKSELA
 * ========================================================================= */

/*
 * Bufor warstwy uzywa 32-bitowych pikseli.
 *
 * Dokladne zero oznacza przezroczystosc:
 */
#define SKLADACZ_PIKSEL_PRZEZROCZYSTY UINT32_C(0x00000000)

/*
 * Widoczny kolor jest ostatecznie przekazywany do grafiki jako 0x00RRGGBB.
 *
 * Poprawiony grafika.cpp moze tymczasowo uzyc gornego bajtu jako markera
 * "widocznej czerni". Kompozytor usuwa ten marker przed zapisem do
 * backbufferu, dlatego kod aplikacji nie powinien interpretowac bitow 31..24
 * jako pelnego kanalu alfa.
 */
#define SKLADACZ_MASKA_KOLORU_RGB UINT32_C(0x00FFFFFF)

/* =========================================================================
 * 3. STRUKTURA WARSTWY
 * ========================================================================= */

/*
 * Metadane jednej powierzchni procesu.
 *
 * Struktura NIE jest packed. Naturalne wyrownanie wskaznika jest
 * bezpieczniejsze dla kernela x86_64.
 *
 * Wlasnosc bufora:
 *
 *   - bufor_pikseli jest tworzony i zwalniany przez skladacz,
 *   - kod zewnetrzny nie moze wykonywac kfree(bufor_pikseli),
 *   - po usun_warstwe() wszystkie poprzednie wskazniki do warstwy/bufora
 *     nalezy traktowac jako niewazne.
 */
struct warstwa_obrazu {
    /*
     * Wlasciciel warstwy.
     * W aktualnym modelu powinien odpowiadac indeksowi tablicy.
     */
    int pid;

    /*
     * Kolejnosc skladania.
     * Mniejsza wartosc = warstwa nizej.
     */
    int z_order;

    /*
     * Pozycja lewego gornego rogu w globalnych wspolrzednych ekranu.
     * Moze byc ujemna - clipping wykonuje skladacz.
     */
    int x;
    int y;

    /*
     * Rozmiar powierzchni w pikselach.
     * Aktywna warstwa zawsze ma wartosci > 0.
     */
    int szerokosc;
    int wysokosc;

    /*
     * Bufor szerokosc * wysokosc pikseli uint32_t.
     *
     * Indeks:
     *
     *   y * szerokosc + x
     *
     * W aktywnej, poprawnej warstwie wskaznik nie jest nullptr.
     */
    uint32_t* bufor_pikseli;

    /*
     * Flaga publikacji warstwy.
     *
     * Poprawiony skladacz odczytuje/zapisuje to pole atomikami
     * acquire/release. Kod zewnetrzny nie powinien ustawiac go recznie.
     */
    bool aktywna;
};

struct GuiDirtyRect {
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
};

enum GuiStanOkna : uint32_t {
    GUI_OKNO_NORMALNE = 0,
    GUI_OKNO_ZMAKSYMALIZOWANE = 1,
    GUI_OKNO_ZMINIMALIZOWANE = 2
};

/* Kopiowalny snapshot ABI taskbara. Nie zawiera wskaznikow kernela. */
struct GuiOknoInfo {
    uint64_t window_id;
    int32_t pid;
    uint32_t generation;
    uint32_t stan;
    uint32_t widoczne;
    uint32_t aktywne;
    int32_t x;
    int32_t y;
    int32_t szerokosc;
    int32_t wysokosc;
    char tytul[48];
};

inline constexpr uint32_t SKLADACZ_MAKS_DIRTY_RECT = 64;

/* =========================================================================
 * 4. KONTROLA ABI warstwa_obrazu
 * ========================================================================= */

#ifdef __cplusplus

static_assert(
    sizeof(int) == 4,
    "Skladacz obrazu wymaga 32-bitowego int"
);

static_assert(
    sizeof(void*) == 8,
    "Skladacz obrazu wymaga kernela x86_64"
);

static_assert(
    offsetof(warstwa_obrazu, pid) == 0x00,
    "Nieprawidlowy offset warstwa_obrazu.pid"
);

static_assert(
    offsetof(warstwa_obrazu, z_order) == 0x04,
    "Nieprawidlowy offset warstwa_obrazu.z_order"
);

static_assert(
    offsetof(warstwa_obrazu, x) == 0x08,
    "Nieprawidlowy offset warstwa_obrazu.x"
);

static_assert(
    offsetof(warstwa_obrazu, y) == 0x0C,
    "Nieprawidlowy offset warstwa_obrazu.y"
);

static_assert(
    offsetof(warstwa_obrazu, szerokosc) == 0x10,
    "Nieprawidlowy offset warstwa_obrazu.szerokosc"
);

static_assert(
    offsetof(warstwa_obrazu, wysokosc) == 0x14,
    "Nieprawidlowy offset warstwa_obrazu.wysokosc"
);

static_assert(
    offsetof(warstwa_obrazu, bufor_pikseli) == 0x18,
    "Nieprawidlowy offset warstwa_obrazu.bufor_pikseli"
);

static_assert(
    offsetof(warstwa_obrazu, aktywna) == 0x20,
    "Nieprawidlowy offset warstwa_obrazu.aktywna"
);

static_assert(
    sizeof(warstwa_obrazu) == 0x28,
    "Zmiana rozmiaru warstwa_obrazu wymaga sprawdzenia skladacza/grafiki"
);

static_assert(
    alignof(warstwa_obrazu) == 8,
    "warstwa_obrazu powinna zachowac naturalne wyrownanie x86_64"
);

#endif /* __cplusplus */

/* =========================================================================
 * 5. GLOBALNA TABLICA WARSTW
 * ========================================================================= */

/*
 * Bezposredni dostep pozostaje publiczny dla zgodnosci z obecnym kodem.
 *
 * Preferowane jest jednak korzystanie z pobierz_warstwe() i funkcji
 * zarzadzajacych, poniewaz poprawiony skladacz prowadzi dodatkowy,
 * prywatny accounting rzeczywistych rozmiarow alokacji.
 *
 * Bezposrednia zmiana szerokosc/wysokosc/bufor_pikseli/aktywna moze zostac
 * celowo uznana przez skladacz za uszkodzenie metadanych.
 */
extern warstwa_obrazu tablica_warstw[
    SKLADACZ_MAKS_WARSTW
];

/* =========================================================================
 * 6. TWORZENIE WARSTWY
 * ========================================================================= */

/*
 * Tworzy albo zastępuje warstwe procesu.
 *
 * Parametry:
 *   pid     - 0..SKLADACZ_MAKS_WARSTW-1,
 *   x/y     - globalna pozycja; moze byc poza ekranem,
 *   szer/wys- dodatnie wymiary powierzchni,
 *   z_order - kolejnosc skladania.
 *
 * Wynik:
 *   pid  - sukces,
 *   -1   - bledne parametry, przekroczony limit albo blad alokacji.
 *
 * Poprawiona implementacja jest transakcyjna względem alokacji:
 * jezeli nie uda sie zaalokowac nowego bufora, istniejaca warstwa pozostaje.
 */
int utworz_warstwe(
    int pid,
    int x,
    int y,
    int szer,
    int wys,
    int z_order
);

/* =========================================================================
 * 7. POZYCJA
 * ========================================================================= */

/*
 * Zmienia globalna pozycje istniejacej aktywnej warstwy.
 *
 * Skrajne/ujemne wspolrzedne sa dozwolone; kompozytor wykonuje clipping
 * z obliczeniami int64_t.
 */
void zaktualizuj_pozycje_warstwy(
    int pid,
    int nowy_x,
    int nowy_y
);

/* =========================================================================
 * 8. CZYSZCZENIE
 * ========================================================================= */

/*
 * Ustawia wszystkie piksele warstwy na:
 *
 *   SKLADACZ_PIKSEL_PRZEZROCZYSTY
 *
 * czyli czysci ja do pelnej przezroczystosci.
 */
void wyczysc_warstwe(
    int pid
);

/* =========================================================================
 * 9. USUWANIE
 * ========================================================================= */

/*
 * Dezaktywuje slot, zwalnia bufor powierzchni i zeruje metadane.
 *
 * Po wywolaniu zadnego wskaznika uzyskanego wczesniej przez
 * pobierz_warstwe() nie wolno dalej uzywac.
 */
void usun_warstwe(
    int pid
);

/* =========================================================================
 * 10. DOSTEP
 * ========================================================================= */

/*
 * Zwraca wskaznik do aktywnego i wewnetrznie spojnego slotu.
 *
 * nullptr:
 *   - PID poza zakresem,
 *   - brak aktywnej warstwy,
 *   - wykryto niespojne/uszkodzone metadane bufora.
 *
 * Wskaznik jest pozyczony (borrowed), nie jest wlasnoscia wywolujacego.
 * Nie nalezy go przechowywac po operacji, ktora moze usunac/zastapic warstwe.
 */
warstwa_obrazu* pobierz_warstwe(
    int pid
);

/* =========================================================================
 * 11. KOMPOZYCJA
 * ========================================================================= */

/*
 * Buduje pelna klatke:
 *
 *   1. przejscie grafiki w surowy tryb skladania,
 *   2. odtworzenie tapety/tla,
 *   3. narysowanie warstw rosnaco po z_order,
 *   4. narysowanie zegara kernela,
 *   5. narysowanie kursora i prezentacja backbufferu.
 *
 * Poprawiona implementacja odrzuca zagniezdzone skladanie tej samej klatki.
 */
void skladacz_obrazu_zloz_klatke();
void skladacz_obrazu_oznacz_dirty();
void skladacz_obrazu_oznacz_dirty_rect(int x, int y, int width, int height);
void skladacz_obrazu_oznacz_ruch_kursora(int old_x, int old_y,
                                         int new_x, int new_y);
void skladacz_obrazu_oznacz_dirty_warstwy(int pid);
void skladacz_obrazu_obsluz_dirty();
void skladacz_obrazu_podnies_warstwe(int pid);
bool skladacz_obrazu_ustaw_tytul(int pid, const char* tytul);
bool skladacz_obrazu_minimalizuj(int pid);
bool skladacz_obrazu_przywroc(uint64_t window_id);
bool skladacz_obrazu_czy_widoczna(int pid);
uint64_t skladacz_obrazu_id_okna(int pid);
uint32_t skladacz_obrazu_snapshot_okien(GuiOknoInfo* out, uint32_t max,
                                        int aktywny_pid);

/* Rejestruje prostokat SYSTEM_OVERLAY nalezacy do warstwy procesu.
 * Warstwa jest nadal skladana normalnie, ale ten fragment jest skladany
 * ponownie po wszystkich APPLICATION i przed kursorem. */
void skladacz_obrazu_ustaw_overlay(int pid, bool otwarty,
                                  int x, int y, int szer, int wys);
int skladacz_obrazu_overlay_pod_punktem(int x, int y);
void skladacz_obrazu_debug_warstwy(const char* powod);

#ifdef __cplusplus

/* =========================================================================
 * 12. LEKKIE HELPERY PUBLICZNE
 * ========================================================================= */

inline constexpr bool skladacz_pid_poprawny(
    int pid
) noexcept {
    return
        pid >= 0 &&
        pid < SKLADACZ_MAKS_WARSTW;
}

inline constexpr bool skladacz_wymiary_poprawne(
    int szerokosc,
    int wysokosc
) noexcept {
    return
        szerokosc > 0 &&
        wysokosc > 0;
}

#endif /* __cplusplus */
