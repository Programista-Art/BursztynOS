/*
 * Bursztyn OS - format aktualizacji systemu (wspoldzielony naglowek).
 *
 * Pliki:
 *   wykazaktualizacji.wykaz - metadane aktualizacji (tekst, pola PL),
 *   bursztyn-X.Y.Z.akt       - pakiet aktualizacji (binarny BAKT).
 *
 * Naglowek jest celowo wolny od zaleznosci jądra (tylkostdint/stddef),
 * dzieki czemu ten sam kod parsujacy jest uzywany przez jadro oraz przez
 * hostowe testy jednostkowe (kompilowane zwyklym g++).
 *
 * Zasady:
 *   - parser fail-closed: kazda anomalia -> blad, zadnych alokacji
 *     zaleznych od danych zdalnych,
 *   - jawne limity dlugosci dla kazdego pola tekstowego,
 *   - brak porownywania wersji jako stringow (krotka numeryczna),
 *   - brak dynamicznej alokacji.
 */

#pragma once

#include <stdint.h>
#include <stddef.h>

/* =====================================================================
 * 1. LIMITY
 * ===================================================================== */

#define AKT_WYKAZ_MAKS_ROZMIAR      8192U
#define AKT_WYKAZ_MAKS_LINIA        768U
#define AKT_WYKAZ_MAKS_KLUCZ        32U
#define AKT_WYKAZ_MAKS_WARTOSC      512U

#define AKT_WERSJA_TEKST_MAKS       15U
#define AKT_PLIK_MAKS               127U
#define AKT_ROZMIAR_TEKST_MAKS      20U
#define AKT_HASH_HEX_DL             64U
#define AKT_PODPIS_HEX_DL           512U
#define AKT_KANAL_TEKST_MAKS        15U

#define AKT_MAKS_ROZMIAR_PAKIETU    (64ULL * 1024ULL * 1024ULL)
#define AKT_MAKS_ROZMIAR_PLIKU_WPISU (16ULL * 1024ULL * 1024ULL)
#define AKT_MAKS_LICZBA_WPISOW      256U

#define AKT_NAGLOWEK_ROZMIAR        16U
#define AKT_WPIS_ROZMIAR            256U
#define AKT_WPIS_KATEGORIA_DL       32U
#define AKT_WPIS_SCIEZKA_DL         128U
#define AKT_WPIS_REZERWA_DL         48U

#define AKT_KANAL_STABILNY  0U
#define AKT_KANAL_TESTOWY   1U
#define AKT_KANAL_ROZWOJOWY 2U

/* Kody bledow parsowania (0 = OK). */
#define AKT_OK                    0
#define AKT_BLAD_ARGUMENT         1
#define AKT_BLAD_ROZMIAR          2
#define AKT_BLAD_UTF8             3
#define AKT_BLAD_NUL              4
#define AKT_BLAD_LINIA            5
#define AKT_BLAD_KLUCZ            6
#define AKT_BLAD_DUPLIKAT         7
#define AKT_BLAD_BRAK_POLA        8
#define AKT_BLAD_WERSJA           9
#define AKT_BLAD_ROZMIAR_POLA     10
#define AKT_BLAD_HASH             11
#define AKT_BLAD_PODPIS           12
#define AKT_BLAD_PLIK             13
#define AKT_BLAD_KANAL            14
#define AKT_BLAD_FORMAT           15
#define AKT_BLAD_PAKIET           16
#define AKT_BLAD_PRZEPELNIENIE    17
#define AKT_BLAD_SCIEZKA          18
#define AKT_BLAD_KATEGORIA        19
#define AKT_BLAD_ZAKRES           20
#define AKT_BLAD_NAKLADANIE       21
#define AKT_BLAD_NIEZGODNOSC      22

#ifdef __cplusplus
extern "C" {
#endif

/* =====================================================================
 * 2. STRUKTURY
 * ===================================================================== */

typedef struct akt_wersja {
    uint32_t major;
    uint32_t minor;
    uint32_t patch;
} akt_wersja_t;

typedef struct akt_wykaz {
    uint32_t wersjaformatu;          /* musi byc 1 */
    akt_wersja_t wersjasystemu;
    akt_wersja_t minimalnawersja;
    char plik[AKT_PLIK_MAKS + 1U];
    uint64_t rozmiar;
    uint8_t sumasha256[32];
    uint8_t podpis[256];             /* surowe bajty podpisu RSA-2048 */
    uint8_t kanal;                   /* AKT_KANAL_* */
    uint8_t ma_kanal;                /* czy pole kanal wystapilo */
} akt_wykaz_t;

/* =====================================================================
 * 3. POMOCNICZE POROWNANIA PAMIECI (bez libc)
 * ===================================================================== */

static inline int akt_porownaj_bajty(const void* a, const void* b, size_t n) {
    const uint8_t* pa = (const uint8_t*)a;
    const uint8_t* pb = (const uint8_t*)b;
    if (n == 0) return 0;
    if (!pa || !pb) return -1;
    for (size_t i = 0; i < n; ++i) {
        if (pa[i] != pb[i]) return pa[i] < pb[i] ? -1 : 1;
    }
    return 0;
}

static inline size_t akt_dlugosc_tekstu(const char* s, size_t limit) {
    if (!s) return 0;
    size_t n = 0;
    while (n < limit && s[n] != '\0') ++n;
    return n;
}

/* =====================================================================
 * 4. WALIDACJA UTF-8 (krotka, surowa, fail-closed)
 * ===================================================================== */

static inline int akt_sprawdz_utf8(const uint8_t* dane, size_t len) {
    if (!dane && len != 0) return AKT_BLAD_ARGUMENT;
    size_t i = 0;
    while (i < len) {
        uint8_t c = dane[i];
        if (c <= 0x7FU) { ++i; continue; }
        size_t need = 0;
        if ((c & 0xE0U) == 0xC0U) need = 1;
        else if ((c & 0xF0U) == 0xE0U) need = 2;
        else if ((c & 0xF8U) == 0xF0U) need = 3;
        else return AKT_BLAD_UTF8;
        /* Nadmiarowe kodowania / surrogaty odrzucamy. */
        if (i + need >= len) return AKT_BLAD_UTF8;
        for (size_t k = 1; k <= need; ++k) {
            if ((dane[i + k] & 0xC0U) != 0x80U) return AKT_BLAD_UTF8;
        }
        uint32_t cp = 0;
        if (need == 1) cp = ((uint32_t)(c & 0x1FU) << 6) | (dane[i+1] & 0x3FU);
        else if (need == 2) cp = ((uint32_t)(c & 0x0FU) << 12) | ((uint32_t)(dane[i+1] & 0x3FU) << 6) | (dane[i+2] & 0x3FU);
        else cp = ((uint32_t)(c & 0x07U) << 18) | ((uint32_t)(dane[i+1] & 0x3FU) << 12) | ((uint32_t)(dane[i+2] & 0x3FU) << 6) | (dane[i+3] & 0x3FU);
        if ((need == 1 && cp < 0x80U) || (need == 2 && cp < 0x800U) || (need == 3 && cp < 0x10000U)) return AKT_BLAD_UTF8;
        if (cp >= 0xD800U && cp <= 0xDFFFU) return AKT_BLAD_UTF8;
        if (cp > 0x10FFFFU) return AKT_BLAD_UTF8;
        i += need + 1;
    }
    return AKT_OK;
}

/* =====================================================================
 * 5. WERSJE major.minor.patch
 * ===================================================================== */

/* Parsuje "X.Y.Z", kazdy skladnik 0..9999, bez znakow, bez spacji. */
static inline int akt_parsuj_wersje(const char* tekst, size_t len, akt_wersja_t* out) {
    if (!tekst || !out) return AKT_BLAD_ARGUMENT;
    if (len == 0 || len > AKT_WERSJA_TEKST_MAKS) return AKT_BLAD_WERSJA;
    uint32_t czesci[3] = {0, 0, 0};
    int czesc = 0;
    size_t cyfry = 0;
    uint32_t wartosc = 0;
    for (size_t i = 0; i < len; ++i) {
        char c = tekst[i];
        if (c == '.') {
            if (cyfry == 0 || czesc >= 2) return AKT_BLAD_WERSJA;
            czesci[czesc] = wartosc;
            ++czesc;
            wartosc = 0;
            cyfry = 0;
            continue;
        }
        if (c < '0' || c > '9') return AKT_BLAD_WERSJA;
        if (cyfry >= 4) return AKT_BLAD_WERSJA;
        uint32_t d = (uint32_t)(c - '0');
        if (wartosc > 999U) return AKT_BLAD_WERSJA;
        wartosc = wartosc * 10U + d;
        if (wartosc > 9999U) return AKT_BLAD_WERSJA;
        ++cyfry;
    }
    if (cyfry == 0 || czesc != 2) return AKT_BLAD_WERSJA;
    czesci[2] = wartosc;
    out->major = czesci[0];
    out->minor = czesci[1];
    out->patch = czesci[2];
    return AKT_OK;
}

/* -1 / 0 / 1 (numerycznie, nie leksykograficznie). */
static inline int akt_porownaj_wersje(const akt_wersja_t* a, const akt_wersja_t* b) {
    if (!a || !b) return 0;
    if (a->major != b->major) return a->major < b->major ? -1 : 1;
    if (a->minor != b->minor) return a->minor < b->minor ? -1 : 1;
    if (a->patch != b->patch) return a->patch < b->patch ? -1 : 1;
    return 0;
}

/* true (1) gdy aktualizacja dozwolona: docelowa > obecna i obecna >= minimalna. */
static inline int akt_czy_dozwolona_aktualizacja(const akt_wersja_t* obecna,
                                                 const akt_wersja_t* docelowa,
                                                 const akt_wersja_t* minimalna) {
    if (!obecna || !docelowa || !minimalna) return 0;
    if (akt_porownaj_wersje(docelowa, obecna) <= 0) return 0; /* ta sama lub downgrade */
    if (akt_porownaj_wersje(obecna, minimalna) < 0) return 0; /* za stara baza */
    return 1;
}

/* =====================================================================
 * 6. HEX / ROZMIAR / KANAL / PLIK
 * ===================================================================== */

static inline int akt_hex_znak(const char c, uint8_t* out) {
    if (c >= '0' && c <= '9') { *out = (uint8_t)(c - '0'); return 1; }
    if (c >= 'a' && c <= 'f') { *out = (uint8_t)(c - 'a' + 10); return 1; }
    return 0; /* wielkie litery celowo odrzucane (kanoniczna mala forma) */
}

static inline int akt_parsuj_hex(const char* tekst, size_t len, uint8_t* out, size_t out_len) {
    if (!tekst || !out) return AKT_BLAD_ARGUMENT;
    if (len != out_len * 2U) return AKT_BLAD_HASH;
    for (size_t i = 0; i < out_len; ++i) {
        uint8_t hi = 0, lo = 0;
        if (!akt_hex_znak(tekst[2U * i], &hi)) return AKT_BLAD_HASH;
        if (!akt_hex_znak(tekst[2U * i + 1U], &lo)) return AKT_BLAD_HASH;
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return AKT_OK;
}

static inline int akt_parsuj_rozmiar(const char* tekst, size_t len, uint64_t* out) {
    if (!tekst || !out) return AKT_BLAD_ARGUMENT;
    if (len == 0 || len > AKT_ROZMIAR_TEKST_MAKS) return AKT_BLAD_ROZMIAR_POLA;
    uint64_t wartosc = 0;
    for (size_t i = 0; i < len; ++i) {
        char c = tekst[i];
        if (c < '0' || c > '9') return AKT_BLAD_ROZMIAR_POLA;
        if (len > 1 && tekst[0] == '0') return AKT_BLAD_ROZMIAR_POLA; /* bez wiodacych zer */
        uint64_t d = (uint64_t)(c - '0');
        if (wartosc > (UINT64_MAX - d) / 10U) return AKT_BLAD_PRZEPELNIENIE;
        wartosc = wartosc * 10U + d;
    }
    if (wartosc == 0 || wartosc > AKT_MAKS_ROZMIAR_PAKIETU) return AKT_BLAD_ROZMIAR_POLA;
    *out = wartosc;
    return AKT_OK;
}

static inline int akt_parsuj_kanal(const char* tekst, size_t len, uint8_t* out) {
    if (!tekst || !out) return AKT_BLAD_ARGUMENT;
    if (len == 8 && akt_porownaj_bajty(tekst, "stabilny", 8) == 0) { *out = AKT_KANAL_STABILNY; return AKT_OK; }
    if (len == 7 && akt_porownaj_bajty(tekst, "testowy", 7) == 0) { *out = AKT_KANAL_TESTOWY; return AKT_OK; }
    if (len == 9 && akt_porownaj_bajty(tekst, "rozwojowy", 9) == 0) { *out = AKT_KANAL_ROZWOJOWY; return AKT_OK; }
    return AKT_BLAD_KANAL;
}

/* Nazwa pakietu: "bursztyn-X.Y.Z.akt", X.Y.Z == wersjasystemu. */
static inline int akt_sprawdz_nazwe_pliku(const char* tekst, size_t len, const akt_wersja_t* wersja) {
    if (!tekst || !wersja) return AKT_BLAD_ARGUMENT;
    if (len < 16 || len > AKT_PLIK_MAKS) return AKT_BLAD_PLIK;
    const char* pref = "bursztyn-";
    for (int i = 0; i < 9; ++i) if (tekst[i] != pref[i]) return AKT_BLAD_PLIK;
    const char* suf = ".akt";
    if (akt_porownaj_bajty(tekst + len - 4, suf, 4) != 0) return AKT_BLAD_PLIK;
    size_t srodek = len - 9U - 4U;
    if (srodek == 0 || srodek > AKT_WERSJA_TEKST_MAKS) return AKT_BLAD_PLIK;
    for (size_t i = 0; i < srodek; ++i) {
        char c = tekst[9 + i];
        if (!((c >= '0' && c <= '9') || c == '.')) return AKT_BLAD_PLIK;
    }
    akt_wersja_t w = {0, 0, 0};
    int rc = akt_parsuj_wersje(tekst + 9, srodek, &w);
    if (rc != AKT_OK) return AKT_BLAD_PLIK;
    if (akt_porownaj_wersje(&w, wersja) != 0) return AKT_BLAD_NIEZGODNOSC;
    return AKT_OK;
}

/* =====================================================================
 * 7. PARSER WYKAZU (.wykaz)
 * ===================================================================== */

static inline int akt_klucz_do_id(const char* k, size_t klen) {
    if (klen == 13 && akt_porownaj_bajty(k, "wersjaformatu", 13) == 0) return 0;
    if (klen == 13 && akt_porownaj_bajty(k, "wersjasystemu", 13) == 0) return 1;
    if (klen == 15 && akt_porownaj_bajty(k, "minimalnawersja", 15) == 0) return 2;
    if (klen == 4 && akt_porownaj_bajty(k, "plik", 4) == 0) return 3;
    if (klen == 7 && akt_porownaj_bajty(k, "rozmiar", 7) == 0) return 4;
    if (klen == 10 && akt_porownaj_bajty(k, "sumasha256", 10) == 0) return 5;
    if (klen == 6 && akt_porownaj_bajty(k, "podpis", 6) == 0) return 6;
    if (klen == 5 && akt_porownaj_bajty(k, "kanal", 5) == 0) return 7;
    return -1;
}

static inline int akt_parsuj_wykaz(const char* dane, size_t len, akt_wykaz_t* out) {
    if (!dane || !out) return AKT_BLAD_ARGUMENT;
    if (len == 0 || len > AKT_WYKAZ_MAKS_ROZMIAR) return AKT_BLAD_ROZMIAR;
    /* Odrzucamy BOM. */
    if (len >= 3 && (uint8_t)dane[0] == 0xEF && (uint8_t)dane[1] == 0xBB && (uint8_t)dane[2] == 0xBF)
        return AKT_BLAD_FORMAT;
    for (size_t i = 0; i < len; ++i) {
        uint8_t c = (uint8_t)dane[i];
        if (c == 0x00) return AKT_BLAD_NUL;
        if (c == '\r') return AKT_BLAD_LINIA;
    }
    if (akt_sprawdz_utf8((const uint8_t*)dane, len) != AKT_OK) return AKT_BLAD_UTF8;
    if (len == 0 || dane[len - 1] != '\n') return AKT_BLAD_LINIA;

    /* Bufory pol z limitami. */
    char w_format[8] = {0}; size_t w_format_len = 0;
    char w_wersja[AKT_WERSJA_TEKST_MAKS + 1U] = {0}; size_t w_wersja_len = 0;
    char w_min[AKT_WERSJA_TEKST_MAKS + 1U] = {0}; size_t w_min_len = 0;
    char w_plik[AKT_PLIK_MAKS + 1U] = {0}; size_t w_plik_len = 0;
    char w_rozmiar[AKT_ROZMIAR_TEKST_MAKS + 1U] = {0}; size_t w_rozmiar_len = 0;
    char w_hash[AKT_HASH_HEX_DL + 1U] = {0}; size_t w_hash_len = 0;
    char w_podpis[AKT_PODPIS_HEX_DL + 1U] = {0}; size_t w_podpis_len = 0;
    char w_kanal[AKT_KANAL_TEKST_MAKS + 1U] = {0}; size_t w_kanal_len = 0;
    uint32_t maska = 0;

    size_t pocz = 0;
    while (pocz < len) {
        size_t koniec = pocz;
        while (koniec < len && dane[koniec] != '\n') ++koniec;
        if (koniec >= len) return AKT_BLAD_LINIA;
        size_t llen = koniec - pocz;
        if (llen == 0) return AKT_BLAD_LINIA;
        if (llen > AKT_WYKAZ_MAKS_LINIA) return AKT_BLAD_LINIA;
        /* Szukamy '='. Dokladnie jeden. */
        size_t eq = 0;
        int znaleziono = 0;
        for (size_t i = 0; i < llen; ++i) {
            if (dane[pocz + i] == '=') {
                if (znaleziono) return AKT_BLAD_LINIA;
                znaleziono = 1;
                eq = i;
            }
        }
        if (!znaleziono) return AKT_BLAD_LINIA;
        size_t klen = eq;
        size_t vlen = llen - eq - 1U;
        if (klen == 0 || klen > AKT_WYKAZ_MAKS_KLUCZ) return AKT_BLAD_KLUCZ;
        if (vlen == 0 || vlen > AKT_WYKAZ_MAKS_WARTOSC) return AKT_BLAD_LINIA;
        const char* k = dane + pocz;
        const char* v = dane + pocz + eq + 1U;
        /* Klucz: tylko [a-z0-9] (np. sumasha256 zawiera cyfry). */
        for (size_t i = 0; i < klen; ++i) {
            char c = k[i];
            int ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
            if (!ok) return AKT_BLAD_KLUCZ;
        }
        /* Wartosc: bez spacji na krancach, bez '=' (juz zapewnione), drukowalne. */
        if (v[0] == ' ' || v[0] == '\t' || v[vlen-1] == ' ' || v[vlen-1] == '\t') return AKT_BLAD_LINIA;
        for (size_t i = 0; i < vlen; ++i) {
            unsigned char c = (unsigned char)v[i];
            if (c < 0x20 || c == 0x7F) return AKT_BLAD_LINIA;
        }
        int id = akt_klucz_do_id(k, klen);
        if (id < 0) return AKT_BLAD_KLUCZ;
        if (maska & (1U << id)) return AKT_BLAD_DUPLIKAT;
        maska |= (1U << id);
        switch (id) {
            case 0:
                if (vlen > 7) return AKT_BLAD_FORMAT;
                for (size_t i = 0; i < vlen && i < 7; ++i) w_format[i] = v[i];
                w_format_len = vlen;
                break;
            case 1:
                if (vlen > AKT_WERSJA_TEKST_MAKS) return AKT_BLAD_WERSJA;
                for (size_t i = 0; i < vlen; ++i) w_wersja[i] = v[i];
                w_wersja_len = vlen;
                break;
            case 2:
                if (vlen > AKT_WERSJA_TEKST_MAKS) return AKT_BLAD_WERSJA;
                for (size_t i = 0; i < vlen; ++i) w_min[i] = v[i];
                w_min_len = vlen;
                break;
            case 3:
                if (vlen > AKT_PLIK_MAKS) return AKT_BLAD_PLIK;
                for (size_t i = 0; i < vlen; ++i) w_plik[i] = v[i];
                w_plik_len = vlen;
                break;
            case 4:
                if (vlen > AKT_ROZMIAR_TEKST_MAKS) return AKT_BLAD_ROZMIAR_POLA;
                for (size_t i = 0; i < vlen; ++i) w_rozmiar[i] = v[i];
                w_rozmiar_len = vlen;
                break;
            case 5:
                if (vlen != AKT_HASH_HEX_DL) return AKT_BLAD_HASH;
                for (size_t i = 0; i < vlen; ++i) w_hash[i] = v[i];
                w_hash_len = vlen;
                break;
            case 6:
                if (vlen != AKT_PODPIS_HEX_DL) return AKT_BLAD_PODPIS;
                for (size_t i = 0; i < vlen; ++i) w_podpis[i] = v[i];
                w_podpis[vlen] = '\0';
                w_podpis_len = vlen;
                break;
            case 7:
                if (vlen > AKT_KANAL_TEKST_MAKS) return AKT_BLAD_KANAL;
                for (size_t i = 0; i < vlen; ++i) w_kanal[i] = v[i];
                w_kanal_len = vlen;
                break;
        }
        pocz = koniec + 1U;
    }

    /* Obowiazkowe: wszystko oprocz kanalu. */
    const uint32_t wymagane = 0x7FU & ~(1U << 7);
    if ((maska & wymagane) != wymagane) return AKT_BLAD_BRAK_POLA;

    /* wersjaformatu == 1 (jedyna wspierana). */
    if (w_format_len != 1 || w_format[0] != '1') return AKT_BLAD_FORMAT;

    akt_wykaz_t w{};
    w.wersjaformatu = 1;
    if (akt_parsuj_wersje(w_wersja, w_wersja_len, &w.wersjasystemu) != AKT_OK) return AKT_BLAD_WERSJA;
    if (akt_parsuj_wersje(w_min, w_min_len, &w.minimalnawersja) != AKT_OK) return AKT_BLAD_WERSJA;
    if (akt_parsuj_rozmiar(w_rozmiar, w_rozmiar_len, &w.rozmiar) != AKT_OK) return AKT_BLAD_ROZMIAR_POLA;
    if (akt_parsuj_hex(w_hash, w_hash_len, w.sumasha256, 32) != AKT_OK) return AKT_BLAD_HASH;
    {
        /* Podpis: dokladnie AKT_PODPIS_HEX_DL (512) hex -> 256 bajtow.
         * Pelna walidacja dlugosci, granic, braku NUL, znakow hex i braku znakow za podpisem. */
        const size_t oczekiwane_bajty = sizeof(w.podpis); /* 256 */
        if (oczekiwane_bajty > (SIZE_MAX / 2U)) return AKT_BLAD_PRZEPELNIENIE;
        const size_t oczekiwana_dlugosc_hex = oczekiwane_bajty * 2U;

        if (w_podpis_len != AKT_PODPIS_HEX_DL) return AKT_BLAD_PODPIS;
        if (w_podpis_len != oczekiwana_dlugosc_hex) return AKT_BLAD_PODPIS;
        if ((w_podpis_len & 1U) != 0U) return AKT_BLAD_PODPIS;
        if (w_podpis_len >= sizeof(w_podpis)) return AKT_BLAD_PODPIS;
        if (w_podpis[w_podpis_len] != '\0') return AKT_BLAD_PODPIS;

        const size_t zdekodowane_bajty = w_podpis_len / 2U;
        if (zdekodowane_bajty != oczekiwane_bajty) return AKT_BLAD_PODPIS;

        uint8_t tmp[sizeof(w.podpis)];
        for (size_t i = 0; i < zdekodowane_bajty; ++i) {
            size_t idx_hi = 2U * i;
            size_t idx_lo = idx_hi + 1U;
            if (idx_lo >= w_podpis_len) return AKT_BLAD_PODPIS;

            char c_hi = w_podpis[idx_hi];
            char c_lo = w_podpis[idx_lo];
            if (c_hi == '\0' || c_lo == '\0') return AKT_BLAD_NUL;

            uint8_t hi = 0, lo = 0;
            if (!akt_hex_znak(c_hi, &hi)) return AKT_BLAD_PODPIS;
            if (!akt_hex_znak(c_lo, &lo)) return AKT_BLAD_PODPIS;
            tmp[i] = (uint8_t)((hi << 4) | lo);
        }
        for (size_t i = 0; i < oczekiwane_bajty; ++i) w.podpis[i] = tmp[i];
    }
    /* Kanal opcjonalny, domyslnie stabilny. */
    if (maska & (1U << 7)) {
        if (akt_parsuj_kanal(w_kanal, w_kanal_len, &w.kanal) != AKT_OK) return AKT_BLAD_KANAL;
        w.ma_kanal = 1;
    } else {
        w.kanal = AKT_KANAL_STABILNY;
        w.ma_kanal = 0;
    }
    for (size_t i = 0; i < w_plik_len; ++i) w.plik[i] = w_plik[i];
    w.plik[w_plik_len] = '\0';
    /* Spojnosc nazwy pliku z wersja. */
    if (akt_sprawdz_nazwe_pliku(w.plik, w_plik_len, &w.wersjasystemu) != AKT_OK) return AKT_BLAD_PLIK;
    /* Wersja minimalna nie moze byc nowsza niz oferowana. */
    if (akt_porownaj_wersje(&w.minimalnawersja, &w.wersjasystemu) > 0) return AKT_BLAD_WERSJA;

    *out = w;
    return AKT_OK;
}

/* Buduje kanoniczne bajty objete podpisem (bez pola podpis).
 * Kolejnosc stala, kanal zawsze jawny. Zwraca dlugosc albo 0 przy bledzie. */
static inline size_t akt_buduj_kanoniczne(const akt_wykaz_t* w, char* out, size_t cap) {
    if (!w || !out || cap < 256) return 0;
    const char* kanal_tekst = "stabilny";
    if (w->kanal == AKT_KANAL_TESTOWY) kanal_tekst = "testowy";
    else if (w->kanal == AKT_KANAL_ROZWOJOWY) kanal_tekst = "rozwojowy";
    else if (w->kanal != AKT_KANAL_STABILNY) return 0;
    size_t kanal_len = akt_dlugosc_tekstu(kanal_tekst, 16);
    size_t plik_len = akt_dlugosc_tekstu(w->plik, AKT_PLIK_MAKS + 1U);
    if (plik_len == 0 || plik_len > AKT_PLIK_MAKS) return 0;
    /* Konwersja wersji na tekst. */
    char vsys[16] = {0}, vmin[16] = {0};
    /* Prosty zapis bez snprintf (freestanding). */
    uint32_t cz[6] = {w->wersjasystemu.major, w->wersjasystemu.minor, w->wersjasystemu.patch,
                      w->minimalnawersja.major, w->minimalnawersja.minor, w->minimalnawersja.patch};
    for (int i = 0; i < 6; ++i) if (cz[i] > 9999U) return 0;
    char* cele[2] = {vsys, vmin};
    size_t clen[2] = {0, 0};
    for (int g = 0; g < 2; ++g) {
        char* cel = cele[g];
        size_t p = 0;
        for (int k = 0; k < 3; ++k) {
            uint32_t v = cz[g * 3 + k];
            char tmp[5]; int tl = 0;
            if (v == 0) { tmp[tl++] = '0'; }
            else {
                char rev[5]; int rl = 0;
                while (v > 0) { rev[rl++] = (char)('0' + (v % 10U)); v /= 10U; }
                while (rl > 0) tmp[tl++] = rev[--rl];
            }
            for (int i = 0; i < tl; ++i) {
                if (p >= 15) return 0;
                cel[p++] = tmp[i];
            }
            if (k < 2) {
                if (p >= 15) return 0;
                cel[p++] = '.';
            }
        }
        cel[p] = '\0';
        clen[g] = p;
    }
    /* rozmiar -> tekst. */
    char roz[21] = {0};
    {
        uint64_t v = w->rozmiar;
        char rev[21]; int rl = 0;
        if (v == 0) return 0;
        while (v > 0) { rev[rl++] = (char)('0' + (v % 10U)); v /= 10U; }
        for (int i = 0; i < rl; ++i) roz[i] = rev[rl - 1 - i];
        roz[rl] = '\0';
    }
    size_t roz_len = akt_dlugosc_tekstu(roz, 21);
    /* hash -> hex (mala litera). */
    char hashhex[65] = {0};
    {
        static const char hx[] = "0123456789abcdef";
        for (int i = 0; i < 32; ++i) {
            hashhex[2*i] = hx[(w->sumasha256[i] >> 4) & 0xFU];
            hashhex[2*i+1] = hx[w->sumasha256[i] & 0xFU];
        }
    }
    /* Skladanie z kontrola pojemnosci. */
    size_t p = 0;
#define AKT_DOPISZ(s, sl) do { \
    if (p + (sl) >= cap) return 0; \
    for (size_t _i = 0; _i < (sl); ++_i) out[p++] = (s)[_i]; \
} while (0)
    AKT_DOPISZ("wersjaformatu=1\n", 16);
    AKT_DOPISZ("wersjasystemu=", 14); AKT_DOPISZ(vsys, clen[0]); AKT_DOPISZ("\n", 1);
    AKT_DOPISZ("minimalnawersja=", 16); AKT_DOPISZ(vmin, clen[1]); AKT_DOPISZ("\n", 1);
    AKT_DOPISZ("plik=", 5); AKT_DOPISZ(w->plik, plik_len); AKT_DOPISZ("\n", 1);
    AKT_DOPISZ("rozmiar=", 8); AKT_DOPISZ(roz, roz_len); AKT_DOPISZ("\n", 1);
    AKT_DOPISZ("sumasha256=", 11); AKT_DOPISZ(hashhex, 64); AKT_DOPISZ("\n", 1);
    AKT_DOPISZ("kanal=", 6); AKT_DOPISZ(kanal_tekst, kanal_len); AKT_DOPISZ("\n", 1);
#undef AKT_DOPISZ
    if (p >= cap) return 0;
    out[p] = '\0';
    return p;
}

/* =====================================================================
 * 8. WALIDACJA PAKIETU .akt (BAKT)
 * ===================================================================== */

static inline uint32_t akt_wczytaj_u32le(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static inline uint64_t akt_wczytaj_u64le(const uint8_t* p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v |= ((uint64_t)p[i]) << (8 * i);
    return v;
}

static inline int akt_dozwolona_kategoria(const char* kat, size_t kat_len) {
    if (!kat) return 0;
    if (kat_len == 5 && akt_porownaj_bajty(kat, "jadro", 5) == 0) return 1;
    if (kat_len == 9 && akt_porownaj_bajty(kat, "aplikacje", 9) == 0) return 1;
    if (kat_len == 10 && akt_porownaj_bajty(kat, "biblioteki", 10) == 0) return 1;
    if (kat_len == 6 && akt_porownaj_bajty(kat, "konfig", 6) == 0) return 1;
    if (kat_len == 4 && akt_porownaj_bajty(kat, "boot", 4) == 0) return 1;
    return 0;
}

/* Sciezka w pakiecie: relatywna, [A-Za-z0-9._-/], bez "..", bez "//", bez '\'. */
static inline int akt_sprawdz_sciezke_wpisu(const char* sciezka, size_t len) {
    if (!sciezka) return AKT_BLAD_ARGUMENT;
    if (len == 0 || len > 127) return AKT_BLAD_SCIEZKA;
    if (sciezka[0] == '/') return AKT_BLAD_SCIEZKA;
    for (size_t i = 0; i < len; ++i) {
        char c = sciezka[i];
        if (c == '\0') return AKT_BLAD_NUL;
        if (c == '\\') return AKT_BLAD_SCIEZKA;
        int ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                 (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-' || c == '/';
        if (!ok) return AKT_BLAD_SCIEZKA;
    }
    if (sciezka[len - 1] == '/') return AKT_BLAD_SCIEZKA;
    /* Komponenty. */
    size_t start = 0;
    for (size_t i = 0; i <= len; ++i) {
        if (i == len || sciezka[i] == '/') {
            size_t clen = i - start;
            if (clen == 0) return AKT_BLAD_SCIEZKA; /* "//" */
            if (clen == 1 && sciezka[start] == '.') return AKT_BLAD_SCIEZKA;
            if (clen == 2 && sciezka[start] == '.' && sciezka[start+1] == '.') return AKT_BLAD_SCIEZKA;
            start = i + 1;
        }
    }
    return AKT_OK;
}

/* Pelna walidacja strukturalna pakietu (bez kryptografii).
 * Zwraca AKT_OK albo kod bledu. entry_count_out opcjonalne. */
static inline int akt_sprawdz_pakiet(const uint8_t* dane, size_t len, uint32_t* entry_count_out) {
    if (!dane) return AKT_BLAD_ARGUMENT;
    if (len < AKT_NAGLOWEK_ROZMIAR) return AKT_BLAD_PAKIET;
    if (len > AKT_MAKS_ROZMIAR_PAKIETU) return AKT_BLAD_ROZMIAR;
    if (!(dane[0] == 'B' && dane[1] == 'A' && dane[2] == 'K' && dane[3] == 'T')) return AKT_BLAD_PAKIET;
    uint32_t format = akt_wczytaj_u32le(dane + 4);
    uint32_t liczba = akt_wczytaj_u32le(dane + 8);
    uint32_t rezerwa = akt_wczytaj_u32le(dane + 12);
    if (format != 1) return AKT_BLAD_FORMAT;
    if (rezerwa != 0) return AKT_BLAD_PAKIET;
    if (liczba == 0 || liczba > AKT_MAKS_LICZBA_WPISOW) return AKT_BLAD_PAKIET;
    /* Overflow: 16 + 256*liczba. */
    uint64_t rozmiar_tabeli = (uint64_t)liczba * (uint64_t)AKT_WPIS_ROZMIAR;
    if (rozmiar_tabeli / AKT_WPIS_ROZMIAR != liczba) return AKT_BLAD_PRZEPELNIENIE;
    uint64_t koniec_naglowka = (uint64_t)AKT_NAGLOWEK_ROZMIAR + rozmiar_tabeli;
    if (koniec_naglowka > (uint64_t)len) return AKT_BLAD_PAKIET;
    if (koniec_naglowka > AKT_MAKS_ROZMIAR_PAKIETU) return AKT_BLAD_ROZMIAR;

    for (uint32_t e = 0; e < liczba; ++e) {
        uint64_t baza = (uint64_t)AKT_NAGLOWEK_ROZMIAR + (uint64_t)e * (uint64_t)AKT_WPIS_ROZMIAR;
        const uint8_t* wpis = dane + (size_t)baza;
        /* Kategoria: NUL w obrebie 32B, reszta zero. */
        size_t kat_len = 0;
        int kat_nul = 0;
        for (size_t i = 0; i < AKT_WPIS_KATEGORIA_DL; ++i) {
            if (wpis[i] == 0) { kat_nul = 1; break; }
            ++kat_len;
        }
        if (!kat_nul || kat_len == 0) return AKT_BLAD_KATEGORIA;
        for (size_t i = kat_len + 1; i < AKT_WPIS_KATEGORIA_DL; ++i) {
            if (wpis[i] != 0) return AKT_BLAD_PAKIET;
        }
        if (!akt_dozwolona_kategoria((const char*)wpis, kat_len)) return AKT_BLAD_KATEGORIA;
        /* Sciezka. */
        const uint8_t* sc = wpis + AKT_WPIS_KATEGORIA_DL;
        size_t sc_len = 0;
        int sc_nul = 0;
        for (size_t i = 0; i < AKT_WPIS_SCIEZKA_DL; ++i) {
            if (sc[i] == 0) { sc_nul = 1; break; }
            ++sc_len;
        }
        if (!sc_nul || sc_len == 0) return AKT_BLAD_SCIEZKA;
        for (size_t i = sc_len + 1; i < AKT_WPIS_SCIEZKA_DL; ++i) {
            if (sc[i] != 0) return AKT_BLAD_PAKIET;
        }
        if (akt_sprawdz_sciezke_wpisu((const char*)sc, sc_len) != AKT_OK) return AKT_BLAD_SCIEZKA;
        /* Offset/rozmiar. */
        uint64_t off = akt_wczytaj_u64le(wpis + AKT_WPIS_KATEGORIA_DL + AKT_WPIS_SCIEZKA_DL);
        uint64_t roz = akt_wczytaj_u64le(wpis + AKT_WPIS_KATEGORIA_DL + AKT_WPIS_SCIEZKA_DL + 8);
        if (roz == 0 || roz > AKT_MAKS_ROZMIAR_PLIKU_WPISU) return AKT_BLAD_ZAKRES;
        if (off > (uint64_t)len) return AKT_BLAD_ZAKRES;
        if (roz > (uint64_t)len) return AKT_BLAD_ZAKRES;
        if (off + roz < off) return AKT_BLAD_PRZEPELNIENIE;
        if (off < koniec_naglowka) return AKT_BLAD_ZAKRES; /* wpis w naglowku */
        if (off + roz > (uint64_t)len) return AKT_BLAD_ZAKRES;
        /* Rezerwa 48B = zero. */
        const uint8_t* rz = wpis + AKT_WPIS_KATEGORIA_DL + AKT_WPIS_SCIEZKA_DL + 8 + 8 + 32;
        for (size_t i = 0; i < AKT_WPIS_REZERWA_DL; ++i) {
            if (rz[i] != 0) return AKT_BLAD_PAKIET;
        }
        /* sha256 moze byc dowolne tutaj - weryfikuje instalator; brak dodatkowych warunkow. */
    }
    /* Duplikaty (kategoria+sciezka) oraz nakladanie zakresow. */
    for (uint32_t a = 0; a < liczba; ++a) {
        uint64_t ba = (uint64_t)AKT_NAGLOWEK_ROZMIAR + (uint64_t)a * (uint64_t)AKT_WPIS_ROZMIAR;
        const uint8_t* wa = dane + (size_t)ba;
        uint64_t oa = akt_wczytaj_u64le(wa + AKT_WPIS_KATEGORIA_DL + AKT_WPIS_SCIEZKA_DL);
        uint64_t ra = akt_wczytaj_u64le(wa + AKT_WPIS_KATEGORIA_DL + AKT_WPIS_SCIEZKA_DL + 8);
        for (uint32_t b = a + 1; b < liczba; ++b) {
            uint64_t bb = (uint64_t)AKT_NAGLOWEK_ROZMIAR + (uint64_t)b * (uint64_t)AKT_WPIS_ROZMIAR;
            const uint8_t* wb = dane + (size_t)bb;
            /* Duplikat nazwy: porownaj kategorie i sciezki (oba NUL-terminated w stalych polach). */
            int same_kat = 1, same_sc = 1;
            for (size_t i = 0; i < AKT_WPIS_KATEGORIA_DL; ++i) {
                if (wa[i] != wb[i]) { same_kat = 0; break; }
                if (wa[i] == 0) break;
            }
            for (size_t i = 0; i < AKT_WPIS_SCIEZKA_DL; ++i) {
                if (wa[AKT_WPIS_KATEGORIA_DL + i] != wb[AKT_WPIS_KATEGORIA_DL + i]) { same_sc = 0; break; }
                if (wa[AKT_WPIS_KATEGORIA_DL + i] == 0) break;
            }
            if (same_kat && same_sc) return AKT_BLAD_DUPLIKAT;
            uint64_t ob = akt_wczytaj_u64le(wb + AKT_WPIS_KATEGORIA_DL + AKT_WPIS_SCIEZKA_DL);
            uint64_t rb = akt_wczytaj_u64le(wb + AKT_WPIS_KATEGORIA_DL + AKT_WPIS_SCIEZKA_DL + 8);
            if (oa < ob + rb && ob < oa + ra) return AKT_BLAD_NAKLADANIE;
        }
    }
    if (entry_count_out) *entry_count_out = liczba;
    return AKT_OK;
}

#ifdef __cplusplus
}
#endif
