#pragma once
/*
 * Snake - ustawienia i rekord (czysta logika serializacji, bez I/O).
 *
 * Plik tekstowy (do 128 B), zapisywany przez ustawienia_zapisz("snake", "stan", ...):
 *   snake1
 *   rekord=<0..231>
 *   predkosc=<0..2>        0 = Wolno, 1 = Normalnie, 2 = Szybko
 *   siatka=<0|1>
 * Wczytanie jest tolerancyjne, ale scisle co do wartosci: brak pliku, zly naglowek,
 * znaki spoza cyfr, liczby spoza zakresu albo plik wiekszy niz limit => wartosc domyslna
 * dla danego pola (rekord 0). Nigdy nie ufa danym z dysku.
 */
#include <stddef.h>
#include <stdint.h>
#include "snake_logika.h"

namespace SnakeLogika {

constexpr size_t USTAWIENIA_MAKS_BAJTOW = 128;
constexpr uint32_t REKORD_MAKS = static_cast<uint32_t>(MAKS_DLUGOSC - DLUGOSC_START);   // 231

struct UstawieniaSnake {
    uint32_t rekord = 0;
    Predkosc predkosc = PREDKOSC_DOMYSLNA;
    bool siatka = true;
    bool poprawne = true;     // false: dane byly uszkodzone/niepelne (uzyto wartosci domyslnych)
};

namespace szczegoly {
inline bool parsuj_liczbe(const char* p, const char* koniec, uint32_t maks, uint32_t* out) {
    if (p >= koniec || koniec - p > 4) return false;
    uint32_t v = 0;
    for (; p < koniec; ++p) {
        if (*p < '0' || *p > '9') return false;
        v = v * 10U + static_cast<uint32_t>(*p - '0');
    }
    if (v > maks) return false;
    *out = v;
    return true;
}
inline bool zaczyna(const char* p, const char* koniec, const char* wzorzec, const char** po) {
    while (*wzorzec) {
        if (p >= koniec || *p != *wzorzec) return false;
        ++p; ++wzorzec;
    }
    *po = p;
    return true;
}
} // namespace szczegoly

inline UstawieniaSnake wczytaj_ustawienia(const char* dane, size_t n) {
    UstawieniaSnake u;
    if (!dane || n == 0 || n > USTAWIENIA_MAKS_BAJTOW) { u.poprawne = (n == 0 && !dane) ? true : false; return u; }
    const char* p = dane;
    const char* koniec = dane + n;
    bool naglowek = false;
    bool ok = true;
    int numer = 0;
    while (p < koniec) {
        const char* kw = p;
        while (kw < koniec && *kw != '\n') ++kw;
        const char* lk = kw;
        if (lk > p && *(lk - 1) == '\r') --lk;
        if (numer == 0) {
            naglowek = (lk - p == 6) && p[0] == 's' && p[1] == 'n' && p[2] == 'a' && p[3] == 'k' && p[4] == 'e' && p[5] == '1';
            if (!naglowek) { ok = false; break; }
        } else if (lk > p) {
            const char* v = nullptr;
            uint32_t liczba = 0;
            if (szczegoly::zaczyna(p, lk, "rekord=", &v)) {
                if (szczegoly::parsuj_liczbe(v, lk, REKORD_MAKS, &liczba)) u.rekord = liczba; else ok = false;
            } else if (szczegoly::zaczyna(p, lk, "predkosc=", &v)) {
                if (szczegoly::parsuj_liczbe(v, lk, 2, &liczba)) u.predkosc = static_cast<Predkosc>(liczba); else ok = false;
            } else if (szczegoly::zaczyna(p, lk, "siatka=", &v)) {
                if (szczegoly::parsuj_liczbe(v, lk, 1, &liczba)) u.siatka = liczba != 0; else ok = false;
            } else ok = false;       // nieznany klucz
        }
        ++numer;
        p = kw < koniec ? kw + 1 : koniec;
    }
    u.poprawne = ok && naglowek;
    return u;
}

// Zwraca dlugosc zapisanego tekstu (bez NUL) albo 0, gdy bufor za maly.
inline size_t zapisz_ustawienia(const UstawieniaSnake& u, char* out, size_t cap) {
    if (!out || cap < 48) return 0;
    uint32_t rekord = u.rekord > REKORD_MAKS ? REKORD_MAKS : u.rekord;
    size_t n = 0;
    auto txt = [&](const char* s) { while (*s) out[n++] = *s++; };
    auto num = [&](uint32_t v) {
        char t[8]; int k = 0;
        do { t[k++] = static_cast<char>('0' + v % 10U); v /= 10U; } while (v);
        while (k) out[n++] = t[--k];
    };
    txt("snake1\nrekord="); num(rekord);
    txt("\npredkosc="); num(static_cast<uint32_t>(u.predkosc) <= 2 ? static_cast<uint32_t>(u.predkosc) : 1U);
    txt("\nsiatka="); num(u.siatka ? 1U : 0U);
    out[n++] = '\n';
    out[n] = '\0';
    return n;
}

} // namespace SnakeLogika
