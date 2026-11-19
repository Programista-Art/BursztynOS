#pragma once
/*
 * Polityka uruchamiania aplikacji systemowych (BWS 10).
 *
 * Problem: ikona na pulpicie uruchamia Ustawienia/Diagnostyke/Aktualizacje,
 * a Eksplorator dostaje "odmowa", bo manifest tych aplikacji ma poziom
 * zaufania < PZB_UZYTKOWNIK i jadro wymaga od rodzica PZB_ZAUFANE +
 * PRAWO_SYSTEM_CONFIG (Menedzer Okien je ma, Eksplorator - slusznie - nie).
 *
 * Rozwiazanie bez nadawania Eksploratorowi uprawnien: wbudowana, skonczona
 * tablica aplikacji systemowych z JAWNYM poziomem i zestawem praw. Rodzic
 * z PRAWO_URUCHOM_PROGRAM (poziom <= PZB_UZYTKOWNIK, nie sandbox) moze
 * uruchomic wylacznie te sciezki, a dziecko dostaje wartosci z tablicy -
 * NIGDY z manifestu na dysku. Manifest w /programy ma etykiete PZB 4 i moze
 * go zmienic zwykly proces z PRAWO_PLIKI_ZAPISZ, wiec nie moze byc zrodlem
 * zaufania przy delegowaniu. Dowolna inna sciezka (np. .cebula uzytkownika
 * z manifestem "poziom_zaufania = 0") nadal wymaga zaufanego rodzica.
 */
#include <stdint.h>
#include <stddef.h>
#include "pzb.h"

namespace uruchamianie {

struct AplikacjaSystemowa {
    const char* sciezka;
    uint8_t poziom;
    uint64_t prawa;
};

inline constexpr AplikacjaSystemowa kAplikacjeSystemowe[] = {
    {"/programy/ustawienia.cebula/ustawienia.bur", PZB_ZAUFANE,
     PRAWO_GUI | PRAWO_PLIKI_CZYTAJ | PRAWO_PLIKI_ZAPISZ | PRAWO_SYSTEM_CONFIG},
    {"/programy/diagnostyka.cebula/diagnostyka.bur", PZB_ZAUFANE,
     PRAWO_GUI | PRAWO_PLIKI_CZYTAJ | PRAWO_PLIKI_ZAPISZ},
    {"/programy/test_dzwieku.cebula/test_dzwieku.bur", PZB_ZAUFANE,
     PRAWO_GUI | PRAWO_PLIKI_CZYTAJ | PRAWO_PLIKI_ZAPISZ},
    {"/programy/aktualizacje.cebula/aktualizacje.bur", PZB_USLUGI,
     PRAWO_GUI | PRAWO_PLIKI_CZYTAJ | PRAWO_PLIKI_ZAPISZ |
     PRAWO_SYSTEM_CONFIG | PRAWO_SIEC},
    /* Menedzer zadan: tylko odczyt telemetrii procesow i konczenie zwyklych aplikacji (BWS 103/104).
     * Bez SYSTEM_CONFIG, zapisu plikow, sieci i uruchamiania programow. */
    {"/programy/menedzer_zadan.cebula/menedzer_zadan.bur", PZB_ZAUFANE,
     PRAWO_GUI | PRAWO_PLIKI_CZYTAJ | PRAWO_ZARZADZANIE_PROCESAMI},
};

constexpr bool rowne(const char* a, const char* b) {
    if (!a || !b) return false;
    size_t i = 0;
    while (a[i] != '\0' && b[i] != '\0' && a[i] == b[i]) ++i;
    return a[i] == '\0' && b[i] == '\0';
}

constexpr const AplikacjaSystemowa* znajdz_aplikacje_systemowa(const char* sciezka) {
    for (const auto& a : kAplikacjeSystemowe)
        if (rowne(a.sciezka, sciezka)) return &a;
    return nullptr;
}

/* Rodzic moze delegowac start aplikacji systemowej bez wlasnej wysokiej
 * rangi: potrzebuje PRAWO_URUCHOM_PROGRAM i nie moze byc sandboxem (5). */
constexpr bool rodzic_moze_delegowac(uint8_t poziom_rodzica, uint64_t prawa_rodzica) {
    return poziom_rodzica <= PZB_UZYTKOWNIK &&
           (prawa_rodzica & PRAWO_URUCHOM_PROGRAM) != 0;
}

/* Kody przyczyn odmowy/niepowodzenia widoczne dla Ring 3 (bezpieczne:
 * zadnych adresow ani danych kryptograficznych). 0 = brak bledu. */
enum KodBledu : uint8_t {
    BLAD_BRAK = 0,
    BLAD_NIEZNANY = 1,
    BLAD_BRAK_PLIKU = 2,        // plik nie istnieje / nie jest plikiem / nie da sie odczytac
    BLAD_FORMAT = 3,            // to nie jest poprawny plik .bur / nieobslugiwana wersja
    BLAD_INTEGRALNOSC = 4,      // podpis / suma kontrolna nie przeszly
    BLAD_JUZ_DZIALA = 5,        // pojedyncza instancja juz dziala
    BLAD_ZASOBY = 6,            // brak slotu procesu / pamieci
    BLAD_UPRAWNIENIE = 7,       // proces nie ma PRAWO_URUCHOM_PROGRAM
    BLAD_ZAUFANIE = 8,          // aplikacja wymaga wyzszego zaufania niz rodzic
    BLAD_ARGUMENT = 9,          // zla sciezka / argument
    BLAD_TWORZENIE_PROCESU = 10,
    BLAD_MAX = 10,
};

inline const char* komunikat_pl(uint8_t kod) {
    switch (kod) {
        case BLAD_BRAK: return "Uruchomiono.";
        case BLAD_BRAK_PLIKU: return "Nie znaleziono pliku programu albo brak dostepu do odczytu.";
        case BLAD_FORMAT: return "To nie jest poprawny program Bursztyn OS (zly format pliku).";
        case BLAD_INTEGRALNOSC: return "Weryfikacja integralnosci programu nie powiodla sie.";
        case BLAD_JUZ_DZIALA: return "Ten program juz dziala - aktywowano istniejace okno.";
        case BLAD_ZASOBY: return "Brak zasobow systemu (pamiec lub miejsce na proces).";
        case BLAD_UPRAWNIENIE: return "Ta aplikacja nie ma uprawnienia do uruchamiania programow.";
        case BLAD_ZAUFANIE: return "Program wymaga wyzszego poziomu zaufania niz aplikacja uruchamiajaca.";
        case BLAD_ARGUMENT: return "Niepoprawna sciezka programu.";
        case BLAD_TWORZENIE_PROCESU: return "Nie udalo sie utworzyc procesu programu.";
        default: return "Nie udalo sie uruchomic programu.";
    }
}

/* Klasyfikacja tekstowego opisu bledu loadera (obraz.blad) na kod. */
constexpr bool zaczyna(const char* s, const char* p) {
    if (!s || !p) return false;
    size_t i = 0;
    while (p[i] != '\0') { if (s[i] != p[i]) return false; ++i; }
    return true;
}

constexpr uint8_t klasyfikuj_blad_loadera(const char* opis) {
    if (!opis) return BLAD_NIEZNANY;
    if (zaczyna(opis, "file-not-found") || zaczyna(opis, "not-a-file") ||
        zaczyna(opis, "header-read-failed") || zaczyna(opis, "image-read-failed") ||
        zaczyna(opis, "filesystem-not-ready")) return BLAD_BRAK_PLIKU;
    if (zaczyna(opis, "signature-")) return BLAD_INTEGRALNOSC;
    if (zaczyna(opis, "truncated-header") || zaczyna(opis, "bad-") ||
        zaczyna(opis, "bur-v2") || zaczyna(opis, "file-size-exceeds") ||
        zaczyna(opis, "invalid-magic") || zaczyna(opis, "file-size-out-of-range") ||
        zaczyna(opis, "text-") || zaczyna(opis, "data-") ||
        zaczyna(opis, "segments-") || zaczyna(opis, "mapped-size") ||
        zaczyna(opis, "zero-fill") || zaczyna(opis, "stack-range") ||
        zaczyna(opis, "entry-") || zaczyna(opis, "unsupported-")) return BLAD_FORMAT;
    if (zaczyna(opis, "already-running")) return BLAD_JUZ_DZIALA;
    if (zaczyna(opis, "no-process-slot") || zaczyna(opis, "image-allocation-failed"))
        return BLAD_ZASOBY;
    if (zaczyna(opis, "invalid-or-overlong") || zaczyna(opis, "invalid-trust-level"))
        return BLAD_ARGUMENT;
    return BLAD_TWORZENIE_PROCESU;
}

} // namespace uruchamianie
