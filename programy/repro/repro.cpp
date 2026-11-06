/*
 * REPRO BWS2/BWS46 - automat reprodukujacy scenariusz:
 *   Pulpit -> Test -> plik/folder w Test -> 3 poziomy.
 * Wynik zapisuje do /logi/repro_wynik.txt i konczy proces.
 */
#include "bursztyn_gui.h"
#include <stddef.h>
#include <stdint.h>

struct NaglowekBur {
    uint8_t magia[4];
    uint64_t punkt_wejscia;
    uint64_t tekst_przesuniecie, tekst_rozmiar, tekst_wirtualny;
    uint64_t dane_przesuniecie, dane_rozmiar, dane_wirtualny;
} __attribute__((packed));

extern "C" __attribute__((noreturn)) void _start();
static constexpr uint64_t TEKST_ROZMIAR = 32768ULL;
static constexpr uint64_t TEKST_WIRTUALNY = 0x1001000ULL;

extern "C" {
__attribute__((section(".naglowek"), used)) NaglowekBur naglowek = {
    {'B', 'U', 'R', '\0'}, reinterpret_cast<uint64_t>(&_start),
    UINT64_C(0x1000), TEKST_ROZMIAR, TEKST_WIRTUALNY,
    TEKST_ROZMIAR + UINT64_C(0x1000), UINT64_C(0x20000),
    TEKST_WIRTUALNY + TEKST_ROZMIAR};
}

namespace {

constexpr size_t RAPORT_CAP = 2048;

char raport[RAPORT_CAP];
size_t raport_dl = 0;

void dopisz(const char* t) {
    while (*t && raport_dl + 1 < RAPORT_CAP) raport[raport_dl++] = *t++;
    raport[raport_dl] = '\0';
}

void dopisz_wynik(const char* co, bool ok) {
    dopisz(co);
    dopisz(ok ? " OK\n" : " FAIL\n");
}

size_t dlugosc_tekstu(const char* t) {
    size_t n = 0;
    while (t[n]) ++n;
    return n;
}

} // namespace

extern "C" __attribute__((noreturn)) void _start() {
    raport_dl = 0;
    raport[0] = '\0';

    const bool k1 = utworz_katalog_uzytkownika("/uzytkownicy/Pulpit/Test");
    dopisz_wynik("BWS46 mkdir /uzytkownicy/Pulpit/Test:", k1);

    const bool p1 = utworz("/uzytkownicy/Pulpit/Test/a.txt");
    dopisz_wynik("BWS2  create /uzytkownicy/Pulpit/Test/a.txt:", p1);

    const bool k2 =
        utworz_katalog_uzytkownika("/uzytkownicy/Pulpit/Test/Folder2");
    dopisz_wynik("BWS46 mkdir /uzytkownicy/Pulpit/Test/Folder2:", k2);

    const bool k3 =
        utworz_katalog_uzytkownika("/uzytkownicy/Pulpit/Test/Folder2/Gleboki");
    dopisz_wynik("BWS46 mkdir /Test/Folder2/Gleboki (poziom 3):", k3);

    const bool p2 =
        utworz("/uzytkownicy/Pulpit/Test/Folder2/Gleboki/x.txt");
    dopisz_wynik("BWS2  create /Test/Folder2/Gleboki/x.txt:", p2);

    bool zap = false;
    if (p2)
        zap = zapisz_plik(
            "/uzytkownicy/Pulpit/Test/Folder2/Gleboki/x.txt", "repro", 5);
    dopisz_wynik("BWS3  write x.txt:", zap);

    BwsMetadanePliku meta{};
    const bool m1 = pobierz_metadane_pliku("/uzytkownicy/Pulpit/Test", &meta);
    if (!m1) {
        dopisz("BWS47 meta Test: FAIL\n");
    } else {
        dopisz("BWS47 meta Test: typ=");
        dopisz(meta.typ == 2 ? "KAT" : (meta.typ == 1 ? "PLIK" : "?"));
        dopisz(" pzb=");
        static const char CYFRY[] = "0123456789";
        char cyfra[2] = {CYFRY[meta.poziom_pzb % 10U], '\0'};
        dopisz(cyfra);
        dopisz(meta.flagi & BWS_META_PZB_DOSTEPNY ? " pzb_ok\n"
                                                   : " brak_pzb\n");
    }

    /* Kontrola poza Pulpitem. */
    const bool k4 = utworz_katalog_uzytkownika("/uzytkownicy/Zwykly");
    dopisz_wynik("BWS46 mkdir /uzytkownicy/Zwykly:", k4);
    const bool p3 = utworz("/uzytkownicy/Zwykly/b.txt");
    dopisz_wynik("BWS2  create /uzytkownicy/Zwykly/b.txt:", p3);

    dopisz("=== KONIEC REPRO ===\n");

    (void)utworz("/logi/repro_wynik.txt");
    const bool zapis_raportu =
        zapisz_plik("/logi/repro_wynik.txt", raport,
                    static_cast<uint32_t>(dlugosc_tekstu(raport)));
    if (!zapis_raportu) {
        (void)utworz("/logi/repro_blad.txt");
        (void)zapisz_plik("/logi/repro_blad.txt",
                          "nie udalo sie zapisac raportu", 28);
    }

    gui_zakoncz_aplikacje();
}
