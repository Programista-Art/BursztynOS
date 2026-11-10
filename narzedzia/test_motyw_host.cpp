/*
 * Bursztyn OS - Test jednostkowy silnika motywów (host test)
 *
 * Sprawdza:
 * 1. ABI v2 (172 B) i stałe struktury, kompatybilność wsteczną ABI v1 (144 B),
 *    dokładny rozmiar produkcyjnego stosu jądra (64 KiB = 65536 B).
 * 2. Poprawność parsowania 5 oficjalnych motywów (.motyw) wraz z 20 rolami kolorystycznymi
 *    i geometrią zaokrągleń Bursztyn Nocny.
 * 3. Walidację fail-closed (odrzucanie nieznanych pól, duplikatów, złych kolorów,
 *    przekroczonych zakresów liczb, braku kontrastu, prób wstrzykiwania kodu),
 *    walidację czystych identyfikatorów motywu (motyw_waliduj_id) oraz
 *    predykat autoryzacji mutacji (motyw_czy_uprawniony_do_mutacji).
 * 4. Formułę zaokrąglania rogów motyw_promien_inset().
 * 5. Transakcyjny podgląd motywu z własnością PID i tokenem transakcji,
 *    15-sekundowym rollbackiem, potwierdzaniem oraz automatycznym sprzątaniem
 *    w razie awarii/zakończenia procesu.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>

#define BURSZTYN_HOST_TEST 1
#include "../motyw.h"
#include "../stos_jadra.h"

// Dołączamy motyw.cpp z flagą host test
#include "../motyw.cpp"

static int g_testy_razem = 0;
static int g_testy_udane = 0;

#define TEST_ASSERT(warunek, opis) do { \
    g_testy_razem++; \
    if (warunek) { \
        g_testy_udane++; \
        printf("[PASS] %s\n", opis); \
    } else { \
        printf("[FAIL] %s (linia %d)\n", opis, __LINE__); \
    } \
} while (0)

void test_abi_i_struktury() {
    printf("\n--- Test 1: ABI v2 (172 B), v1 (144 B), paleta i stos produkcyjny ---\n");
    TEST_ASSERT(sizeof(BursztynMotyw) == 172, "sizeof(BursztynMotyw) == 172 B (ABI v2)");
    TEST_ASSERT(sizeof(BursztynMotywV1) == 144, "sizeof(BursztynMotywV1) == 144 B (ABI v1)");
    TEST_ASSERT(MOTYW_KOLOR_LICZBA == 20, "20 ról kolorystycznych");
    TEST_ASSERT(MOTYW_ROG_LICZBA == 6, "6 ról promieni zaokrągleń");

    // Weryfikacja produkcyjnego rozmiaru stosu jądra: dokładnie 64 KiB (65536 bajtów)
    TEST_ASSERT(ROZMIAR_STOSU_JADRA == 65536ULL, "Produkcyjny stos jadra ma dokladnie 64 KiB (65536 B)");
    TEST_ASSERT(ROZMIAR_STOSU_JADRA_BAJTY == 65536ULL, "ROZMIAR_STOSU_JADRA_BAJTY == 65536 B");

    // Fallback Bursztyn Nocny ABI v2
    TEST_ASSERT(g_domyslny_motyw_fallback.wersja == 2, "Fallback wersja == 2");
    TEST_ASSERT(g_domyslny_motyw_fallback.rozmiar_struktury == 172, "Fallback rozmiar_struktury == 172");
    TEST_ASSERT(strcmp(g_domyslny_motyw_fallback.id, "bursztynnocny") == 0, "Fallback id == bursztynnocny");
    TEST_ASSERT(strcmp(g_domyslny_motyw_fallback.nazwa, "Bursztyn Nocny") == 0, "Fallback nazwa == Bursztyn Nocny");

    // Dokładna paleta Bursztyn Nocny
    TEST_ASSERT(g_domyslny_motyw_fallback.pulpit == 0x00111315U, "Fallback pulpit == #111315");
    TEST_ASSERT(g_domyslny_motyw_fallback.oknotlo == 0x00191C20U, "Fallback oknotlo == #191C20");
    TEST_ASSERT(g_domyslny_motyw_fallback.panel == 0x0020242AU, "Fallback panel == #20242A");
    TEST_ASSERT(g_domyslny_motyw_fallback.podniesiony == 0x00262B31U, "Fallback podniesiony == #262B31");
    TEST_ASSERT(g_domyslny_motyw_fallback.ramka == 0x00343A42U, "Fallback ramka == #343A42");
    TEST_ASSERT(g_domyslny_motyw_fallback.tekst == 0x00F2F3F5U, "Fallback tekst == #F2F3F5");
    TEST_ASSERT(g_domyslny_motyw_fallback.tekstdrugorzedny == 0x00A7ADB5U, "Fallback tekstdrugorzedny == #A7ADB5");
    TEST_ASSERT(g_domyslny_motyw_fallback.wylaczony == 0x006F7680U, "Fallback wylaczony == #6F7680");
    TEST_ASSERT(g_domyslny_motyw_fallback.akcent == 0x00F2A900U, "Fallback akcent == #F2A900");
    TEST_ASSERT(g_domyslny_motyw_fallback.akcentnajechany == 0x00FFBC32U, "Fallback akcentnajechany == #FFBC32");
    TEST_ASSERT(g_domyslny_motyw_fallback.akcentaktywny == 0x00D99100U, "Fallback akcentaktywny == #D99100");
    TEST_ASSERT(g_domyslny_motyw_fallback.blad == 0x00E85D5DU, "Fallback blad == #E85D5D");
    TEST_ASSERT(g_domyslny_motyw_fallback.sukces == 0x0051B87AU, "Fallback sukces == #51B87A");
    TEST_ASSERT(g_domyslny_motyw_fallback.ostrzezenie == 0x00D97706U, "Fallback ostrzezenie == #D97706");
    TEST_ASSERT(g_domyslny_motyw_fallback.informacja == 0x0058A6E7U, "Fallback informacja == #58A6E7");

    // Geometria
    TEST_ASSERT(g_domyslny_motyw_fallback.rogokna == 10, "Fallback rogokna == 10");
    TEST_ASSERT(g_domyslny_motyw_fallback.rogpanelu == 8, "Fallback rogpanelu == 8");
    TEST_ASSERT(g_domyslny_motyw_fallback.rogprzycisku == 7, "Fallback rogprzycisku == 7");
    TEST_ASSERT(g_domyslny_motyw_fallback.rogpola == 6, "Fallback rogpola == 6");
    TEST_ASSERT(g_domyslny_motyw_fallback.rogmenu == 8, "Fallback rogmenu == 8");
    TEST_ASSERT(g_domyslny_motyw_fallback.rogpodpowiedzi == 6, "Fallback rogpodpowiedzi == 6");
    TEST_ASSERT(g_domyslny_motyw_fallback.gruboscramki == 1, "Fallback gruboscramki == 1");
    TEST_ASSERT(g_domyslny_motyw_fallback.cienokna == 1, "Fallback cienokna == 1");

    TEST_ASSERT(motyw_waliduj(&g_domyslny_motyw_fallback), "Fallback motyw_waliduj() == true");
    TEST_ASSERT(motyw_waliduj_kontrast(&g_domyslny_motyw_fallback), "Fallback motyw_waliduj_kontrast() == true");
}

void test_parsowanie_oficjalnych_motywow() {
    printf("\n--- Test 2: Parsowanie 5 oficjalnych deklaracji .motyw (ABI v2) ---\n");
    BursztynMotyw m{};

    // 1. Bursztyn Nocny
    TEST_ASSERT(motyw_parsuj(PLIK_BURSZTYN_NOCNY, strlen(PLIK_BURSZTYN_NOCNY), &m), "Parsowanie Bursztyn Nocny");
    TEST_ASSERT(m.wersja == 2, "Bursztyn Nocny wersja == 2");
    TEST_ASSERT(m.rozmiar_struktury == 172, "Bursztyn Nocny rozmiar_struktury == 172");
    TEST_ASSERT(strcmp(m.id, "bursztynnocny") == 0, "Bursztyn Nocny id");
    TEST_ASSERT(strcmp(m.nazwa, "Bursztyn Nocny") == 0, "Bursztyn Nocny nazwa");
    TEST_ASSERT(m.pulpit == 0x00111315U, "Bursztyn Nocny pulpit == #111315");
    TEST_ASSERT(m.oknotlo == 0x00191C20U, "Bursztyn Nocny oknotlo == #191C20");
    TEST_ASSERT(m.panel == 0x0020242AU, "Bursztyn Nocny panel == #20242A");
    TEST_ASSERT(m.podniesiony == 0x00262B31U, "Bursztyn Nocny podniesiony == #262B31");
    TEST_ASSERT(m.ramka == 0x00343A42U, "Bursztyn Nocny ramka == #343A42");
    TEST_ASSERT(m.tekst == 0x00F2F3F5U, "Bursztyn Nocny tekst == #F2F3F5");
    TEST_ASSERT(m.tekstdrugorzedny == 0x00A7ADB5U, "Bursztyn Nocny tekstdrugorzedny == #A7ADB5");
    TEST_ASSERT(m.wylaczony == 0x006F7680U, "Bursztyn Nocny wylaczony == #6F7680");
    TEST_ASSERT(m.akcent == 0x00F2A900U, "Bursztyn Nocny akcent == #F2A900");
    TEST_ASSERT(m.akcentnajechany == 0x00FFBC32U, "Bursztyn Nocny akcentnajechany == #FFBC32");
    TEST_ASSERT(m.akcentaktywny == 0x00D99100U, "Bursztyn Nocny akcentaktywny == #D99100");
    TEST_ASSERT(m.blad == 0x00E85D5DU, "Bursztyn Nocny blad == #E85D5D");
    TEST_ASSERT(m.sukces == 0x0051B87AU, "Bursztyn Nocny sukces == #51B87A");
    TEST_ASSERT(m.ostrzezenie == 0x00D97706U, "Bursztyn Nocny ostrzezenie == #D97706");
    TEST_ASSERT(m.informacja == 0x0058A6E7U, "Bursztyn Nocny informacja == #58A6E7");
    TEST_ASSERT(m.rogokna == 10, "Bursztyn Nocny rogokna == 10");
    TEST_ASSERT(m.rogpanelu == 8, "Bursztyn Nocny rogpanelu == 8");
    TEST_ASSERT(m.rogprzycisku == 7, "Bursztyn Nocny rogprzycisku == 7");
    TEST_ASSERT(m.rogpola == 6, "Bursztyn Nocny rogpola == 6");
    TEST_ASSERT(m.rogmenu == 8, "Bursztyn Nocny rogmenu == 8");
    TEST_ASSERT(m.rogpodpowiedzi == 6, "Bursztyn Nocny rogpodpowiedzi == 6");
    TEST_ASSERT(m.gruboscramki == 1, "Bursztyn Nocny gruboscramki == 1");
    TEST_ASSERT(m.cienokna == 1, "Bursztyn Nocny cienokna == 1");
    TEST_ASSERT(motyw_waliduj_kontrast(&m), "Bursztyn Nocny walidacja kontrastu");

    // 2. Bałtycka Noc
    memset(&m, 0, sizeof(m));
    TEST_ASSERT(motyw_parsuj(PLIK_BALTYCKA_NOC, strlen(PLIK_BALTYCKA_NOC), &m), "Parsowanie Bałtycka Noc");
    TEST_ASSERT(m.wersja == 2, "Bałtycka Noc wersja == 2");
    TEST_ASSERT(strcmp(m.id, "baltyckanoc") == 0, "Bałtycka Noc id");
    TEST_ASSERT(m.pulpit == 0x000B1118U, "Bałtycka Noc pulpit == #0B1118");
    TEST_ASSERT(m.akcent == 0x0035B7C8U, "Bałtycka Noc akcent == #35B7C8");
    TEST_ASSERT(motyw_waliduj_kontrast(&m), "Bałtycka Noc walidacja kontrastu");

    // 3. Grafit
    memset(&m, 0, sizeof(m));
    TEST_ASSERT(motyw_parsuj(PLIK_GRAFIT, strlen(PLIK_GRAFIT), &m), "Parsowanie Grafit");
    TEST_ASSERT(m.wersja == 2, "Grafit wersja == 2");
    TEST_ASSERT(strcmp(m.id, "grafit") == 0, "Grafit id");
    TEST_ASSERT(m.pulpit == 0x00121212U, "Grafit pulpit == #121212");
    TEST_ASSERT(m.akcent == 0x008FA3B8U, "Grafit akcent == #8FA3B8");
    TEST_ASSERT(motyw_waliduj_kontrast(&m), "Grafit walidacja kontrastu");

    // 4. Bursztyn Klasyczny
    memset(&m, 0, sizeof(m));
    TEST_ASSERT(motyw_parsuj(PLIK_BURSZTYN_KLASYCZNY, strlen(PLIK_BURSZTYN_KLASYCZNY), &m), "Parsowanie Bursztyn Klasyczny");
    TEST_ASSERT(m.wersja == 2, "Bursztyn Klasyczny wersja == 2");
    TEST_ASSERT(strcmp(m.id, "bursztynklasyczny") == 0, "Bursztyn Klasyczny id");
    TEST_ASSERT(m.pulpit == 0x00170D07U, "Bursztyn Klasyczny pulpit == #170D07");
    TEST_ASSERT(m.akcent == 0x00D98A00U, "Bursztyn Klasyczny akcent == #D98A00");
    TEST_ASSERT(motyw_waliduj_kontrast(&m), "Bursztyn Klasyczny walidacja kontrastu");

    // 5. Bursztyn Jasny
    memset(&m, 0, sizeof(m));
    TEST_ASSERT(motyw_parsuj(PLIK_BURSZTYN_JASNY, strlen(PLIK_BURSZTYN_JASNY), &m), "Parsowanie Bursztyn Jasny");
    TEST_ASSERT(m.wersja == 2, "Bursztyn Jasny wersja == 2");
    TEST_ASSERT(strcmp(m.id, "bursztynjasny") == 0, "Bursztyn Jasny id");
    TEST_ASSERT(m.pulpit == 0x00EDEBE7U, "Bursztyn Jasny pulpit == #EDEBE7");
    TEST_ASSERT(m.oknotlo == 0x00F7F6F3U, "Bursztyn Jasny oknotlo == #F7F6F3");
    TEST_ASSERT(m.tekst == 0x00202124U, "Bursztyn Jasny tekst == #202124");
    TEST_ASSERT(m.tekstdrugorzedny == 0x005F6368U, "Bursztyn Jasny tekstdrugorzedny == #5F6368");
    TEST_ASSERT(m.wylaczony == 0x008A8F98U, "Bursztyn Jasny wylaczony == #8A8F98");
    TEST_ASSERT(m.akcent == 0x00B87200U, "Bursztyn Jasny akcent == #B87200");
    TEST_ASSERT(m.akcentnajechany == 0x00D99100U, "Bursztyn Jasny akcentnajechany == #D99100");
    TEST_ASSERT(m.akcentaktywny == 0x009A5F00U, "Bursztyn Jasny akcentaktywny == #9A5F00");
    TEST_ASSERT(m.zaznaczenie == 0x00FFE082U, "Bursztyn Jasny zaznaczenie == #FFE082");
    TEST_ASSERT(m.przycisk == 0x00E9E6DFU, "Bursztyn Jasny przycisk == #E9E6DF");
    TEST_ASSERT(m.przycisknajechany == 0x00DDD9D0U, "Bursztyn Jasny przycisknajechany == #DDD9D0");
    TEST_ASSERT(m.blad == 0x00BA1A1AU, "Bursztyn Jasny blad == #BA1A1A");
    TEST_ASSERT(m.sukces == 0x001E7E34U, "Bursztyn Jasny sukces == #1E7E34");
    TEST_ASSERT(m.ostrzezenie == 0x00995200U, "Bursztyn Jasny ostrzezenie == #995200");
    TEST_ASSERT(m.informacja == 0x001864ABU, "Bursztyn Jasny informacja == #1864AB");
    TEST_ASSERT(motyw_waliduj_kontrast(&m), "Bursztyn Jasny walidacja kontrastu");
}

void test_bezpieczenstwo_i_fail_closed() {
    printf("\n--- Test 3: Bezpieczeństwo parsowania, fail-closed, walidacja ID i autoryzacja ---\n");
    BursztynMotyw m{};

    // Próba wstrzyknięcia nieznanego klucza / kodu
    const char* zly_skrypt =
        "wersja = 2\n"
        "id = test\n"
        "nazwa = Test\n"
        "exec = /bin/sh\n"
        "pulpit = #111315\n";
    TEST_ASSERT(!motyw_parsuj(zly_skrypt, strlen(zly_skrypt), &m), "Odrzucenie nieznanego klucza 'exec'");

    // Duplikat klucza
    const char* zly_duplikat =
        "wersja = 2\n"
        "id = test\n"
        "id = test2\n"
        "nazwa = Test\n";
    TEST_ASSERT(!motyw_parsuj(zly_duplikat, strlen(zly_duplikat), &m), "Odrzucenie zduplikowanego klucza");

    // Zły format koloru (brak hasha)
    const char* zly_kolor1 =
        "wersja = 2\n"
        "id = test\n"
        "nazwa = Test\n"
        "pulpit = 111315\n";
    TEST_ASSERT(!motyw_parsuj(zly_kolor1, strlen(zly_kolor1), &m), "Odrzucenie koloru bez '#'");

    // Zły format koloru (niepoprawny hex)
    const char* zly_kolor2 =
        "wersja = 2\n"
        "id = test\n"
        "nazwa = Test\n"
        "pulpit = #GG1234\n";
    TEST_ASSERT(!motyw_parsuj(zly_kolor2, strlen(zly_kolor2), &m), "Odrzucenie niepoprawnego heksadecymalnego koloru");

    // Przekroczony promień rogu (> 32)
    const char* zly_promien =
        "wersja = 2\n"
        "id = test\n"
        "nazwa = Test\n"
        "rog_okno = 33\n";
    TEST_ASSERT(!motyw_parsuj(zly_promien, strlen(zly_promien), &m), "Odrzucenie promienia > 32");

    // Przekroczona grubość ramki (> 8)
    const char* zla_ramka =
        "wersja = 2\n"
        "id = test\n"
        "nazwa = Test\n"
        "grubosc_ramki = 9\n";
    TEST_ASSERT(!motyw_parsuj(zla_ramka, strlen(zla_ramka), &m), "Odrzucenie grubości ramki > 8");

    // Przekroczony cień (> 1)
    const char* zly_cien =
        "wersja = 2\n"
        "id = test\n"
        "nazwa = Test\n"
        "cien_okna = 2\n";
    TEST_ASSERT(!motyw_parsuj(zly_cien, strlen(zly_cien), &m), "Odrzucenie cienia > 1");

    // Brak wymaganych pól (obcięty plik)
    const char* obciety =
        "wersja = 2\n"
        "id = test\n";
    TEST_ASSERT(!motyw_parsuj(obciety, strlen(obciety), &m), "Odrzucenie obciętego pliku motywu");

    // Przekroczenie limitu rozmiaru (> 4096 B)
    char* duzy_plik = (char*)malloc(5000);
    assert(duzy_plik != NULL);
    memset(duzy_plik, '#', 4500);
    duzy_plik[4500] = '\0';
    TEST_ASSERT(!motyw_parsuj(duzy_plik, 4500, &m), "Odrzucenie pliku > 4096 bajtów");
    free(duzy_plik);

    // Odrzucenie motywu z brakiem kontrastu (czarny tekst na czarnym tle)
    BursztynMotyw zly_kontrast = g_domyslny_motyw_fallback;
    zly_kontrast.tekst = 0x000000;
    zly_kontrast.oknotlo = 0x000000;
    TEST_ASSERT(!motyw_waliduj_kontrast(&zly_kontrast), "Odrzucenie braku kontrastu (tekst vs oknotlo)");
    TEST_ASSERT(!motyw_waliduj(&zly_kontrast), "motyw_waliduj() odrzuca brak kontrastu");

    // Walidacja identyfikatorów motywów (motyw_waliduj_id)
    TEST_ASSERT(motyw_waliduj_id("bursztynnocny"), "Walidacja id 'bursztynnocny' OK");
    TEST_ASSERT(motyw_waliduj_id("bursztyn_nocny"), "Walidacja id 'bursztyn_nocny' OK");
    TEST_ASSERT(motyw_waliduj_id("baltyckanoc"), "Walidacja id 'baltyckanoc' OK");
    TEST_ASSERT(!motyw_waliduj_id("bursztyn/nocny"), "Odrzucenie id z ukosnikiem");
    TEST_ASSERT(!motyw_waliduj_id("../etc/passwd"), "Odrzucenie sciezki traversal");
    TEST_ASSERT(!motyw_waliduj_id("BursztynNocny"), "Odrzucenie wielkich liter");
    TEST_ASSERT(!motyw_waliduj_id("motyw ze spacja"), "Odrzucenie spacji w id");
    TEST_ASSERT(!motyw_waliduj_id(""), "Odrzucenie pustego id");
    TEST_ASSERT(!motyw_waliduj_id(nullptr), "Odrzucenie nullptr id");

    // Weryfikacja predykatu autoryzacji (motyw_czy_uprawniony_do_mutacji)
    proces_t p_unauth{};
    p_unauth.poziom_zaufania = PZB_UZYTKOWNIK;
    p_unauth.uprawnienia = 0;
    TEST_ASSERT(!motyw_czy_uprawniony_do_mutacji(&p_unauth), "Zwykly proces bez uprawnien odrzucony");

    proces_t p_zaufane_no_perm{};
    p_zaufane_no_perm.poziom_zaufania = PZB_ZAUFANE;
    p_zaufane_no_perm.uprawnienia = PRAWO_GUI;
    TEST_ASSERT(!motyw_czy_uprawniony_do_mutacji(&p_zaufane_no_perm), "PZB_ZAUFANE bez PRAWO_SYSTEM_CONFIG odrzucony");

    proces_t p_auth{};
    p_auth.poziom_zaufania = PZB_ZAUFANE;
    p_auth.uprawnienia = PRAWO_SYSTEM_CONFIG;
    TEST_ASSERT(motyw_czy_uprawniony_do_mutacji(&p_auth), "PZB_ZAUFANE z PRAWO_SYSTEM_CONFIG poprawnie autoryzowany");

    proces_t p_kernel{};
    p_kernel.poziom_zaufania = PZB_JADRO;
    p_kernel.uprawnienia = PRAWO_SYSTEM_CONFIG;
    TEST_ASSERT(motyw_czy_uprawniony_do_mutacji(&p_kernel), "PZB_JADRO z PRAWO_SYSTEM_CONFIG poprawnie autoryzowany");
}

void test_zaokraglenia_rogow() {
    printf("\n--- Test 4: Formuła zaokrąglania rogów motyw_promien_inset() ---\n");

    // r = 0
    TEST_ASSERT(motyw_promien_inset(0, 0) == 0, "r=0 daje inset=0");

    // r = 8 (np. róg panelu)
    // dy = 0..7 (od brzegu zewnętrznego w głąb: dy=0 daje max inset, dy=r-1 daje 0)
    int prev_inset = 8;
    bool monotoniczny = true;
    for (int dy = 0; dy < 8; ++dy) {
        int ins = motyw_promien_inset(8, dy);
        if (ins > prev_inset || ins < 0 || ins >= 8) {
            monotoniczny = false;
        }
        prev_inset = ins;
    }
    TEST_ASSERT(monotoniczny, "Wcięcia (inset) są monotoniczne i w granicach [0, r)");
    TEST_ASSERT(motyw_promien_inset(8, 7) == 0, "Wcięcie na dolnej krawędzi łuku (dy=r-1) wynosi 0");
    TEST_ASSERT(motyw_promien_inset(8, 0) > 0, "Wcięcie na szczycie łuku (dy=0) jest dodatnie");

    // dy poza zakresem
    TEST_ASSERT(motyw_promien_inset(8, -1) == 0, "dy < 0 daje 0");
    TEST_ASSERT(motyw_promien_inset(8, 8) == 0, "dy >= r daje 0");
    TEST_ASSERT(motyw_promien_inset(8, 100) == 0, "dy >> r daje 0");
}

void test_transakcja_podgladu_i_rollback() {
    printf("\n--- Test 5: Transakcja podglądu (15 s), tokeny, własność PID i rollback ---\n");
    motyw_inicjalizuj();

    const BursztynMotyw* poczatkowy = motyw_pobierz_aktywny();
    TEST_ASSERT(strcmp(poczatkowy->id, "bursztynnocny") == 0, "Początkowy motyw: bursztynnocny");

    // 1. Uruchomienie podglądu Bałtyckiej Nocy z tokenem transakcji
    uint64_t t0 = 10000;
    TEST_ASSERT(motyw_rozpocznij_podglad("baltyckanoc", 42, t0), "Rozpoczęcie podglądu baltyckanoc");
    TEST_ASSERT(strcmp(motyw_pobierz_aktywny()->id, "baltyckanoc") == 0, "Aktywny motyw w podglądzie: baltyckanoc");
    uint64_t token1 = g_podglad_token;
    TEST_ASSERT(token1 > 0, "Wygenerowano unikalny token transakcji");

    // Inny proces nie może anulować lub zatwierdzić transakcji procesu 42
    TEST_ASSERT(!motyw_zatwierdz_podglad_token(100, token1), "Obcy PID 100 nie moze zatwierdzic transakcji PID 42");
    TEST_ASSERT(!motyw_przywroc_podglad_token(100, token1), "Obcy PID 100 nie moze przywrocic transakcji PID 42");
    // Błędny token jest odrzucany
    TEST_ASSERT(!motyw_zatwierdz_podglad_token(42, token1 + 999), "Bledny token zatwierdzenia odrzucony");

    // 2. Ręczne przywrócenie (rollback) z poprawnym PID i tokenem
    TEST_ASSERT(motyw_przywroc_podglad_token(42, token1), "Ręczne przywrócenie podglądu z wlasciwym tokenem");
    TEST_ASSERT(strcmp(motyw_pobierz_aktywny()->id, "bursztynnocny") == 0, "Po przywróceniu aktywny: bursztynnocny");

    // 3. Uruchomienie podglądu i zatwierdzenie (commit)
    TEST_ASSERT(motyw_rozpocznij_podglad("grafit", 42, t0), "Rozpoczęcie podglądu grafit");
    uint64_t token2 = g_podglad_token;
    TEST_ASSERT(token2 > token1, "Nowy token transakcji jest monotoniczny");
    TEST_ASSERT(strcmp(motyw_pobierz_aktywny()->id, "grafit") == 0, "Aktywny w podglądzie: grafit");
    TEST_ASSERT(motyw_zatwierdz_podglad_token(42, token2), "Zatwierdzenie podglądu z poprawnym tokenem");
    TEST_ASSERT(strcmp(motyw_pobierz_aktywny()->id, "grafit") == 0, "Po zatwierdzeniu aktywny: grafit");

    // Próba przywrócenia po zatwierdzeniu powinna być odrzucona
    TEST_ASSERT(!motyw_przywroc_podglad(42), "Przywrócenie po zatwierdzeniu odrzucone");

    // 4. Test automatycznego rollbacku po 15 sekundach (timeout)
    t0 = 20000;
    TEST_ASSERT(motyw_rozpocznij_podglad("bursztynjasny", 42, t0), "Rozpoczęcie podglądu bursztynjasny");
    TEST_ASSERT(strcmp(motyw_pobierz_aktywny()->id, "bursztynjasny") == 0, "W trakcie podglądu: bursztynjasny");

    // Czas t0 + 10s (jeszcze nie upłynął)
    motyw_sprawdz_timeout_podgladu(t0 + 10000);
    TEST_ASSERT(strcmp(motyw_pobierz_aktywny()->id, "bursztynjasny") == 0, "Po 10s nadal: bursztynjasny");

    // Czas t0 + 15s + 1ms (upłynął limit 15000 ms)
    motyw_sprawdz_timeout_podgladu(t0 + 15001);
    TEST_ASSERT(strcmp(motyw_pobierz_aktywny()->id, "grafit") == 0, "Po 15.001 s automatyczny rollback do grafit");

    // 5. Test awarii procesu podglądającego (crash / exit cleanup)
    TEST_ASSERT(motyw_rozpocznij_podglad("baltyckanoc", 99, t0), "Rozpoczęcie podglądu z PID 99");
    TEST_ASSERT(strcmp(motyw_pobierz_aktywny()->id, "baltyckanoc") == 0, "Podgląd PID 99: baltyckanoc");

    // Inny proces próbuje sprzątać - nie rusza transakcji procesu 99
    motyw_posprzataj_proces(100);
    TEST_ASSERT(strcmp(motyw_pobierz_aktywny()->id, "baltyckanoc") == 0, "Inny PID 100 nie anuluje transakcji PID 99");

    // Proces 99 ulega awarii / kończy się
    motyw_posprzataj_proces(99);
    TEST_ASSERT(strcmp(motyw_pobierz_aktywny()->id, "grafit") == 0, "Po awarii PID 99 natychmiastowy rollback do grafit");
}

int main() {
    printf("=== Testy jednostkowe natywnego podsystemu motywów Bursztyn OS ===\n");

    test_abi_i_struktury();
    test_parsowanie_oficjalnych_motywow();
    test_bezpieczenstwo_i_fail_closed();
    test_zaokraglenia_rogow();
    test_transakcja_podgladu_i_rollback();

    printf("\n==================================================\n");
    printf("Podsumowanie testów: %d / %d udanych (%.1f%%)\n",
           g_testy_udane, g_testy_razem,
           g_testy_razem > 0 ? (g_testy_udane * 100.0 / g_testy_razem) : 0.0);
    printf("==================================================\n");

    return (g_testy_udane == g_testy_razem) ? 0 : 1;
}
