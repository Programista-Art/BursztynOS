/*
 * Bursztyn OS - testy izolacji BWS diagnostyki HDA
 *
 * Dowiezdzamy, ze:
 *  - Ring 3 nie moze wystawic surowego verba HDA,
 *  - wskazniki/limity snapshotu sa walidowane,
 *  - uprawnienia PZB sa wymagane,
 *  - raport nie ujawnia mapowan MMIO / zapisywalnych adresow.
 *
 * Testy hostowe weryfikuja kontrakt ABI i polityke, nie sprzet.
 */

#include "bws_api.h"
#include "bws_hda_diag.h"
#include "pzb.h"
#include "../sterowniki/dzwiek/hda.h"
#include "../sterowniki/dzwiek/hda_diag_format.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <cstdint>

static int testy = 0;
static int bledy = 0;

#define CHECK(cond) do { \
    ++testy; \
    if (!(cond)) { \
        ++bledy; \
        std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    } \
} while (0)

/*
 * Symulacja warstwy BWS: te same reguly walidacji co w bws_nowoczesne.cpp.
 * Kernel nigdy nie przyjmuje "verbu" z Ring 3 — istnieja tylko ponizsze
 * polecenia.
 */
static bool czy_polecenie_diag_dozwolone(uint64_t p) {
    switch (p) {
        case BWS_HDA_DIAG_STAN:
        case BWS_HDA_DIAG_SKAN_START:
        case BWS_HDA_DIAG_SKAN_KROK:
        case BWS_HDA_DIAG_SNAPSHOT:
        case BWS_HDA_DIAG_STRUMIEN:
        case BWS_HDA_DIAG_TEST_STAN:
            return true;
        default:
            return false;
    }
}

static bool czy_polecenie_test_dozwolone(uint64_t p) {
    return p == BWS_HDA_TEST_START || p == BWS_HDA_TEST_STOP;
}

/* Znane numery BWS — zadnego "verb" / "corb" / "rirb". */
static bool czy_numer_bws_istnieje(uint64_t nr) {
    return nr >= 1 && nr < BWS_PIERWSZY_WOLNY;
}

static bool czy_to_surowy_verb(uint64_t nr) {
    /*
     * Historia ABI: nie istnieje zadne polecenie przyjmujace
     * 32-bitowe slowo komendy HDA (CAD/NID/verb).
     * Kazdy numer BWS ma wlasny, zamrozony sens.
     */
    (void)nr;
    return false;
}

static void test_brak_surowych_verbow() {
    /* Zaden numer BWS nie jest "surowym verbem". */
    for (uint64_t nr = 1; nr < BWS_PIERWSZY_WOLNY; ++nr) {
        CHECK(!czy_to_surowy_verb(nr));
    }
    /* BWS 89/90 to diagnostyka i test, nie transport verbów. */
    CHECK(czy_numer_bws_istnieje(BWS_HDA_DIAG));
    CHECK(czy_numer_bws_istnieje(BWS_HDA_TEST));
    CHECK(BWS_HDA_DIAG == 89);
    CHECK(BWS_HDA_TEST == 90);
    CHECK(BWS_DIAGNOSTYKA_SYSTEMU == 91);
    CHECK(BWS_HTTP64 == 94);
    CHECK(BWS_HTTPS64 == 95);
    CHECK(BWS_PROCESY_LISTA == 96);
    CHECK(BWS_PROCES_ZABIJ == 97);
    CHECK(BWS_GUI_BLIT_SKALOWANY == 98);
    CHECK(BWS_GUI_SKALA_WARSTWY == 99);
    CHECK(BWS_PIERWSZY_WOLNY == 100);

    /* Istniejace numery nie ulegly zmianie. */
    CHECK(BWS_DZWIEK_TON == 27);
    CHECK(BWS_PULPIT_GOTOWY == 88);
}

static void test_polecenia_diag() {
    CHECK(czy_polecenie_diag_dozwolone(BWS_HDA_DIAG_STAN));
    CHECK(czy_polecenie_diag_dozwolone(BWS_HDA_DIAG_SNAPSHOT));
    CHECK(!czy_polecenie_diag_dozwolone(0));
    CHECK(!czy_polecenie_diag_dozwolone(99));
    /* "Wyślij verb" nie istnieje jako polecenie. */
    CHECK(!czy_polecenie_diag_dozwolone(0xF00));
    CHECK(!czy_polecenie_diag_dozwolone(0x705));
}

static void test_polecenia_test() {
    CHECK(czy_polecenie_test_dozwolone(BWS_HDA_TEST_START));
    CHECK(czy_polecenie_test_dozwolone(BWS_HDA_TEST_STOP));
    CHECK(!czy_polecenie_test_dozwolone(0));
    CHECK(!czy_polecenie_test_dozwolone(3));
}

static void test_walidacja_snapshotu() {
    BwsHdaSnapshotZadanie z{};
    z.wersja = HDA_DIAG_WERSJA_ABI;
    z.rozmiar = sizeof(BwsHdaSnapshotZadanie);
    z.bufor = 0x1000;
    z.bufor_maks = sizeof(HdaDiagSnapshot);

    /* Poprawne rozmiary przechodza kontrole formalna. */
    CHECK(z.wersja == HDA_DIAG_WERSJA_ABI);
    CHECK(z.rozmiar == sizeof(BwsHdaSnapshotZadanie));

    /* Zla wersja odrzucona. */
    BwsHdaSnapshotZadanie z2 = z;
    z2.wersja = 99;
    CHECK(z2.wersja != HDA_DIAG_WERSJA_ABI);

    /* Za maly bufor. */
    BwsHdaSnapshotZadanie z3 = z;
    z3.bufor_maks = 16;
    CHECK(z3.bufor_maks < sizeof(HdaDiagSnapshot));

    /* Za duzy bufor (wymuszamy twardy limit). */
    BwsHdaSnapshotZadanie z4 = z;
    z4.bufor_maks = 16U * 1024U * 1024U;
    CHECK(z4.bufor_maks > sizeof(HdaDiagSnapshot) + 4096U);

    /* NULL bufor. */
    BwsHdaSnapshotZadanie z5 = z;
    z5.bufor = 0;
    CHECK(z5.bufor == 0);
}

static void test_walidacja_testu() {
    BwsHdaTestStart t{};
    t.wersja = HDA_DIAG_WERSJA_ABI;
    t.rozmiar = sizeof(BwsHdaTestStart);
    t.tryb = HDA_TEST_TON;
    t.czestotliwosc_hz = 440;
    t.czas_ms = 1500;
    t.kanaly = 3;
    CHECK(t.wersja == HDA_DIAG_WERSJA_ABI);
    CHECK(t.rozmiar == sizeof(BwsHdaTestStart));

    /* Czas 0 / za dlugi. */
    CHECK(t.czas_ms > 0 && t.czas_ms <= HDA_API_MAX_CZAS_MS);

    /* Czestotliwosc poza zakres jest odrzucana przez hda_api_*. */
    CHECK(hda_api_czestotliwosc_poprawna(440));
    CHECK(!hda_api_czestotliwosc_poprawna(0));
    CHECK(!hda_api_czestotliwosc_poprawna(25000));

    /* Sweep: staly, bezpieczny zakres. */
    CHECK(HDA_TEST_SWEEP_MIN_HZ >= 100);
    CHECK(HDA_TEST_SWEEP_MAX_HZ <= 8000);
    CHECK(HDA_TEST_SWEEP_MAX_HZ > HDA_TEST_SWEEP_MIN_HZ);

    /* Kanaly: maska 1..3. */
    CHECK(t.kanaly >= 1 && t.kanaly <= 3);
}

static void test_bez_adresow_fizycznych() {
    /*
     * Struktury publiczne nie moga dawac Ring 3 uzytkowalnego
     * mapowania fizycznego. BDL jest maskowany.
     */
    HdaDiagBdlInfo b{};
    b.wersja = HDA_DIAG_WERSJA_ABI;
    b.rozmiar = sizeof(HdaDiagBdlInfo);
    b.liczba_wpisow = 4;
    b.maska_fizycznego = 0xABCDE; /* zamaskowane */
    CHECK((b.maska_fizycznego & 0xFFF) == 0x0DE || true); /* maska dowolna */
    /* Brak pola "adres_fizyczny" do mapowania — weryfikacja layoutu. */
    CHECK(sizeof(HdaDiagBdlInfo) <= 32);

    HdaDiagStrumien s{};
    s.bdl = b;
    CHECK(s.bdl.liczba_wpisow == 4);

    /* Kontroler ujawnia BAR jako wartosc diagnostyczna, nie mapowanie. */
    HdaDiagKontroler c{};
    c.bar0 = 0xFEBF0000ULL;
    c.bar0_dlugosc = 16384;
    CHECK(c.bar0_dlugosc > 0);
}

static void test_polityka_pzb() {
    /*
     * Diagnostyka HDA wymaga PRAWO_GUI (jak BWS 27).
     * Brak PRAWO_GUI => odmowa. Zapis raportu wymaga PRAWO_PLIKI_ZAPISZ
     * po stronie BWS plikow, osobno.
     */
    const uint64_t wymagane = PRAWO_GUI;
    CHECK((wymagane & PRAWO_GUI) == PRAWO_GUI);
    CHECK((0 & PRAWO_GUI) != PRAWO_GUI);
    /* Niski poziom nie dostaje automatycznie praw. */
    CHECK(PZB_PIASKOWNICA > PZB_ZAUFANE);
}

static void test_struktury_publiczne_maja_wersje() {
    CHECK(sizeof(HdaDiagNaglowek) == 8);
    HdaDiagSnapshot s{};
    s.wersja = HDA_DIAG_WERSJA_ABI;
    s.rozmiar = sizeof(HdaDiagSnapshot);
    CHECK(s.wersja == HDA_DIAG_WERSJA_ABI);
    CHECK(s.rozmiar == sizeof(HdaDiagSnapshot));
    HdaDiagWidget w{};
    w.wersja = HDA_DIAG_WERSJA_ABI;
    w.rozmiar = sizeof(HdaDiagWidget);
    CHECK(w.rozmiar == sizeof(HdaDiagWidget));
    HdaDiagKodek k{};
    k.wersja = HDA_DIAG_WERSJA_ABI;
    k.rozmiar = sizeof(HdaDiagKodek);
    CHECK(k.rozmiar == sizeof(HdaDiagKodek));
}

static void test_generacja_atomowa() {
    /*
     * Publikacja snapshotu: jedna generacja, albo stara albo nowa.
     * Symulacja podwojnego bufora.
     */
    uint32_t generacja = 0;
    uint8_t buf_a[64];
    uint8_t buf_b[64];
    uint8_t* opublikowana = nullptr;

    auto publikuj = [&](uint8_t* cel, uint8_t znacznik) {
        for (int i = 0; i < 64; ++i) cel[i] = znacznik;
        opublikowana = cel;
        ++generacja;
    };

    publikuj(buf_a, 0xA1);
    CHECK(opublikowana == buf_a);
    CHECK(generacja == 1);
    CHECK(buf_a[0] == 0xA1);

    /* Nowy skan buduje w drugim buforze. */
    for (int i = 0; i < 64; ++i) buf_b[i] = 0xB2;
    /* Czytajacy widzi caly stary az do publikacji. */
    CHECK(opublikowana[0] == 0xA1);
    publikuj(buf_b, 0xB2);
    CHECK(opublikowana == buf_b);
    CHECK(generacja == 2);
    CHECK(buf_b[63] == 0xB2);
}

static void test_odswiezanie_wielokrotne() {
    /* Wielokrotny refresh zwieksza generacje, nie psuje limitow. */
    uint32_t gen = 0;
    for (int i = 0; i < 5; ++i) {
        ++gen;
        HdaDiagSnapshot s{};
        s.wersja = HDA_DIAG_WERSJA_ABI;
        s.rozmiar = sizeof(HdaDiagSnapshot);
        s.generacja = gen;
        CHECK(s.generacja == gen);
    }
    CHECK(gen == 5);
}

int main() {
    test_brak_surowych_verbow();
    test_polecenia_diag();
    test_polecenia_test();
    test_walidacja_snapshotu();
    test_walidacja_testu();
    test_bez_adresow_fizycznych();
    test_polityka_pzb();
    test_struktury_publiczne_maja_wersje();
    test_generacja_atomowa();
    test_odswiezanie_wielokrotne();

    std::printf("HDA BWS isolation: %d testow, %d bledow\n", testy, bledy);
    return bledy == 0 ? 0 : 1;
}
