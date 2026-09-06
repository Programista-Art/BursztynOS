#pragma once
#include <stdint.h>
#include <stddef.h>
#include "obrazy/skalowanie.h"

#if defined(BURSZTYNDIAGTAPETY) && !defined(BURSZTYN_DIAG_TAPETY)
#define BURSZTYN_DIAG_TAPETY BURSZTYNDIAGTAPETY
#endif
#ifndef BURSZTYN_DIAG_TAPETY
#define BURSZTYN_DIAG_TAPETY 0
#endif

constexpr uint32_t TAPETY_LICZBA = 10;
constexpr uint32_t TAPETY_MINI_W = 160, TAPETY_MINI_H = 90;
constexpr size_t TAPETY_SCIEZKA_MAX = 128, TAPETY_CFG_MAX = 512;
constexpr uint32_t TAPETY_PLIK_MAX = 16U * 1024U * 1024U;
constexpr uint32_t TAPETY_PIKSELE_MAX = 4U * 1024U * 1024U;
constexpr uint32_t TAPETY_TLO = 0xFF1A0B00U;
constexpr const char* TAPETY_CFG = "/ustawienia/pulpit.cfg";
constexpr const char* TAPETY_PROGRAM = "/programy/tapety.cebula/tapety.bur";
constexpr const char* TAPETY_TEST_PROGRAM = "/programy/test.cebula/test.bur";
inline constexpr const char* TAPETY_SCIEZKI[TAPETY_LICZBA] = {
    "/tapety/bursztyn01.btp", "/tapety/bursztyn02.btp", "/tapety/bursztyn03.btp",
    "/tapety/bursztyn04.btp", "/tapety/bursztyn05.btp", "/tapety/bursztyn06.btp",
    "/tapety/bursztyn07.btp", "/tapety/bursztyn08.btp", "/tapety/bursztyn09.btp",
    "/tapety/bursztyn10.btp"
};
inline constexpr const char* TAPETY_NAZWY[TAPETY_LICZBA] = {
    "Bursztyn", "Bałtyk", "Gdańsk", "Noc nad Bałtykiem", "Złoty bursztyn",
    "Morski błękit", "Polska geometria", "Jesień", "Zachód słońca", "Ciemny bursztyn"
};
inline bool tapety_rowne(const char* a, const char* b, size_t cap = TAPETY_SCIEZKA_MAX) {
    if (!a || !b) return false;
    for (size_t i = 0; i < cap; ++i) {
        if (a[i] != b[i]) return false;
        if (!a[i]) return true;
    }
    return false;
}
inline bool tapety_kopiuj(char* dst, const char* src, size_t cap) {
    if (!dst || !src || !cap) return false;
    for (size_t i = 0; i < cap; ++i) { dst[i] = src[i]; if (!src[i]) return true; }
    dst[cap - 1] = 0;
    return false;
}
inline bool tapety_sciezka(const char* s) {
    if (!s) return false;
    size_t n = 0;
    while (n < TAPETY_SCIEZKA_MAX && s[n]) ++n;
    if (n >= TAPETY_SCIEZKA_MAX || n < 13) return false;
    constexpr char prefix[] = "/tapety/";
    for (size_t i = 0; i < sizeof(prefix) - 1; ++i) if (s[i] != prefix[i]) return false;
    if (!tapety_rowne(s + n - 4, ".btp", 5)) return false;
    // Jeden basename; bez ukrytych plikow, separatorow, traversal i kontrolnych.
    if (s[8] == '.') return false;
    for (size_t i = 8; i < n;) {
        const uint8_t c = static_cast<uint8_t>(s[i]);
        if (c < 128) {
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '_' || c == '-' || c == ' ' || c == '.')) return false;
            if (c == '.' && i + 1 < n && s[i + 1] == '.') return false;
            ++i;
            continue;
        }
        uint32_t cp = 0, ile = 0, minimum = 0;
        if (c >= 0xC2 && c <= 0xDF) { cp = c & 31U; ile = 2; minimum = 0x80; }
        else if (c >= 0xE0 && c <= 0xEF) { cp = c & 15U; ile = 3; minimum = 0x800; }
        else if (c >= 0xF0 && c <= 0xF4) { cp = c & 7U; ile = 4; minimum = 0x10000; }
        else return false;
        if (ile > n - i) return false;
        for (uint32_t j = 1; j < ile; ++j) {
            const uint8_t d = static_cast<uint8_t>(s[i + j]);
            if ((d & 0xC0) != 0x80) return false;
            cp = (cp << 6) | (d & 63U);
        }
        if (cp < minimum || cp < 0xA0 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return false;
        i += ile;
    }
    return true;
}
struct TapetyKonfiguracja { char sciezka[TAPETY_SCIEZKA_MAX]; TrybSkalowaniaObrazu tryb; };
inline TapetyKonfiguracja tapety_domyslna() {
    TapetyKonfiguracja c{};
    tapety_kopiuj(c.sciezka, TAPETY_SCIEZKI[0], sizeof(c.sciezka));
    c.tryb = TrybSkalowaniaObrazu::Wypelnij;
    return c;
}
inline const char* tapety_nazwa_trybu(TrybSkalowaniaObrazu t) {
    return t == TrybSkalowaniaObrazu::Dopasuj ? "dopasuj" :
           t == TrybSkalowaniaObrazu::Wypelnij ? "wypelnij" :
           t == TrybSkalowaniaObrazu::Wysrodkuj ? "wysrodkuj" : "";
}
inline bool tapety_parsuj(const char* buf, size_t n, TapetyKonfiguracja* out) {
    if (!out) return false;
    *out = tapety_domyslna();
    if (!buf || !n || n > TAPETY_CFG_MAX) return false;
    for (size_t i = 0; i < n; ++i) if (!buf[i]) return false;
    constexpr char poczatek[] = "tapeta=";
    if (n < 7) return false;
    for (size_t i = 0; i < 7; ++i) if (buf[i] != poczatek[i]) return false;
    size_t i = 7, p = 0;
    TapetyKonfiguracja c{};
    while (i < n && buf[i] != '\n' && p + 1 < sizeof(c.sciezka)) c.sciezka[p++] = buf[i++];
    if (i >= n || buf[i++] != '\n' || !tapety_sciezka(c.sciezka) || n - i < 6) return false;
    constexpr char klucz[] = "tryb=";
    for (size_t j = 0; j < 5; ++j) if (buf[i++] != klucz[j]) return false;
    char tryb[16]{};
    p = 0;
    while (i < n && buf[i] != '\n' && p + 1 < sizeof(tryb)) tryb[p++] = buf[i++];
    if (i >= n || buf[i++] != '\n' || i != n) return false;
    if (tapety_rowne(tryb, "dopasuj", sizeof(tryb))) c.tryb = TrybSkalowaniaObrazu::Dopasuj;
    else if (tapety_rowne(tryb, "wypelnij", sizeof(tryb))) c.tryb = TrybSkalowaniaObrazu::Wypelnij;
    else if (tapety_rowne(tryb, "wysrodkuj", sizeof(tryb))) c.tryb = TrybSkalowaniaObrazu::Wysrodkuj;
    else return false;
    *out = c;
    return true;
}
inline size_t tapety_serializuj(const TapetyKonfiguracja& c, char* buf, size_t cap) {
    if (!buf || !tapety_sciezka(c.sciezka) || static_cast<uint32_t>(c.tryb) > 2) return 0;
    const char* czesci[] = {"tapeta=", c.sciezka, "\ntryb=", tapety_nazwa_trybu(c.tryb), "\n"};
    size_t n = 0;
    for (const char* s : czesci) for (size_t i = 0; s[i]; ++i) {
        if (n >= cap) return 0;
        buf[n++] = s[i];
    }
    return n;
}

// Protokol wewnetrznej uslugi przez NIEZMIENIONE BWS 68..71.
// Slot 0/generacja 1 jest rezerwowany przez jadro przed pierwszym procesem.
constexpr uint64_t TAPETY_IPC = UINT64_C(0x100000001);
enum TapetyOperacja : uint32_t { TAPETY_STAN = 1, TAPETY_ZASTOSUJ = 2, TAPETY_DIAGNOSTYKA = 3 };
enum TapetyStatus : uint32_t { TAPETY_OK = 0, TAPETY_BLAD = 1, TAPETY_BLAD_ZAPISU = 2 };
enum TapetyZrodlo : uint32_t { TAPETY_SOLID = 0, TAPETY_BTP = 1, TAPETY_BMP = 2 };

// Wspolna, testowalna kolejnosc boot fallback; callbacki wykonuja realna walidacje.
template<class Btp, class Bmp, class Solid>
TapetyZrodlo tapety_fallback(const TapetyKonfiguracja& wybrana, Btp btp, Bmp bmp, Solid solid) {
    if (tapety_sciezka(wybrana.sciezka) && static_cast<uint32_t>(wybrana.tryb) <= 2 && btp(wybrana))
        return TAPETY_BTP;
    const TapetyKonfiguracja dom = tapety_domyslna();
    if ((!tapety_rowne(wybrana.sciezka, dom.sciezka) || wybrana.tryb != dom.tryb) && btp(dom))
        return TAPETY_BTP;
    if (bmp()) return TAPETY_BMP;
    solid();
    return TAPETY_SOLID;
}
struct TapetyZadanie {
    uint32_t wersja, operacja, tryb, grupa;
    uint64_t odpowiedz;
    char sciezka[TAPETY_SCIEZKA_MAX];
};
struct TapetyOdpowiedz {
    uint32_t wersja, status, tryb, zrodlo;
    uint32_t szerokosc, wysokosc, poprawne, bledy;
    uint64_t bufor_bajty, scratch_bajty, odczyty, dekodowania, przygotowania, zmiany;
    uint64_t odczyt_us, dekoduj_us, skaluj_us, heap_przed, heap_po, adres_bufora;
    char sciezka[TAPETY_SCIEZKA_MAX];
};
static_assert(sizeof(TapetyZadanie) == 152, "Wewnetrzny protokol Tapety");
static_assert(sizeof(TapetyOdpowiedz) <= 512, "Odpowiedz musi zmiescic sie w BWS IPC");
