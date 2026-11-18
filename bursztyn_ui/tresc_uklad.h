#pragma once
/*
 * Uklad tresci informacyjnej okien podrzednych (czysta logika, bez zaleznosci od UI - testy hosta).
 *
 * Format tresci (UTF-8):
 *   "# Naglowek"        - naglowek sekcji (akcent)
 *   "Pole<TAB>Wartosc"  - wlasciwosc w dwoch kolumnach (obie kolumny zawijaja sie, nic nie jest obcinane)
 *   pusta linia         - odstep
 *   inny tekst          - akapit zawijany do szerokosci
 * Mierzenie tekstu dostarcza wywolujacy (aktualna czcionka BSF i skala UI): Miernik(const char*, size_t) -> piksele.
 * Zawijanie jest po slowach; slowo szersze od kolumny jest lamane po znakach UTF-8 (nigdy nie jest ucinane).
 * Zadne ograniczenie rozmiaru nie powoduje ciszy: przy przepelnieniu tablicy linii uklad ustawia `przepelnienie`.
 */
#include <stddef.h>
#include <stdint.h>

namespace TrescUklad {

enum class Typ : uint8_t { Akapit, Naglowek, Klucz, Wartosc };

inline constexpr size_t MAKS_LINII = 768;
inline constexpr size_t MAKS_BLOKOW = 192;

struct Linia {
    Typ typ;
    int x;          // przesuniecie w poziomie od lewej krawedzi tresci
    int y;          // pozycja gorna wiersza (bez przewijania)
    uint16_t poczatek;
    uint16_t dlugosc;
};

struct Uklad {
    Linia linie[MAKS_LINII];
    size_t liczba = 0;
    int wysokosc = 0;           // pelna wysokosc tresci w pikselach
    int szerokosc_klucza = 0;   // szerokosc kolumny kluczy (0 = brak wlasciwosci)
    bool przepelnienie = false;
};

inline size_t dl_utf8(unsigned char c) { return c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 1; }

// Zawija [t, t+dl) do `szer` px; wola emit(poczatek_wzgledny, dlugosc) dla kazdej linii. Zwraca liczbe linii.
template <class Miernik, class Emit>
inline size_t zawin(const char* t, size_t dl, int szer, Miernik& mierz, Emit&& emit) {
    size_t linie = 0, i = 0;
    if (szer < 1) szer = 1;
    while (i < dl) {
        while (i < dl && t[i] == ' ') ++i;
        if (i >= dl) break;
        const size_t start = i;
        size_t koniec = start;      // koniec ostatniego slowa, ktore sie miesci
        size_t j = start;
        while (j < dl) {
            size_t k = j;           // koniec slowa zaczynajacego sie w j
            while (k < dl && t[k] != ' ') k += dl_utf8(static_cast<unsigned char>(t[k]));
            if (k > dl) k = dl;
            if (mierz(t + start, k - start) > szer) break;
            koniec = k;
            j = k;
            while (j < dl && t[j] == ' ') ++j;
        }
        if (koniec == start) {      // pojedyncze slowo szersze od kolumny: lamiemy po znakach UTF-8 (min. 1 znak)
            size_t c = start;
            while (c < dl && t[c] != ' ') {
                size_t n = dl_utf8(static_cast<unsigned char>(t[c]));
                if (c + n > dl) n = dl - c;
                if (c > start && mierz(t + start, c + n - start) > szer) break;
                c += n;
            }
            koniec = c > start ? c : start + 1;
        }
        emit(start, koniec - start);
        ++linie;
        i = koniec;
    }
    return linie;
}

// Buduje uklad. wys_linii: wysokosc wiersza tekstu w px, odstep: dodatkowy odstep akapitow/sekcji w px.
template <class Miernik>
inline void zbuduj(Uklad& u, const char* tekst, size_t dl, int szerokosc, int wys_linii, int odstep, Miernik& mierz) {
    u.liczba = 0; u.wysokosc = 0; u.szerokosc_klucza = 0; u.przepelnienie = false;
    if (!tekst || szerokosc < 8) return;
    // Przebieg 1: szerokosc kolumny kluczy = najszerszy klucz, ale nie wiecej niz 40% szerokosci.
    int najszerszy = 0;
    for (size_t p = 0; p < dl;) {
        size_t k = p; while (k < dl && tekst[k] != '\n') ++k;
        for (size_t q = p; q < k; ++q) if (tekst[q] == '\t') { const int w = mierz(tekst + p, q - p); if (w > najszerszy) najszerszy = w; break; }
        p = k + 1;
    }
    const int odstep_kolumn = odstep * 2 > 8 ? odstep * 2 : 8;
    if (najszerszy > 0) {
        int kw = najszerszy + odstep_kolumn / 2;
        const int limit = szerokosc * 2 / 5;
        if (kw > limit) kw = limit;
        if (kw < 24) kw = 24;
        u.szerokosc_klucza = kw;
    }
    const int x_wart = u.szerokosc_klucza + odstep_kolumn;
    int y = 0;
    bool poprzedni_pusty = true;
    auto dodaj = [&](Typ typ, int x, size_t poczatek, size_t dlugosc, int yy) {
        if (u.liczba >= MAKS_LINII) { u.przepelnienie = true; return false; }
        u.linie[u.liczba++] = Linia{typ, x, yy, static_cast<uint16_t>(poczatek), static_cast<uint16_t>(dlugosc)};
        return true;
    };
    for (size_t p = 0; p <= dl;) {
        size_t k = p; while (k < dl && tekst[k] != '\n') ++k;
        const size_t len = k - p;
        if (len == 0) { if (!poprzedni_pusty) y += odstep; poprzedni_pusty = true; p = k + 1; continue; }
        size_t tab = len; for (size_t q = 0; q < len; ++q) if (tekst[p + q] == '\t') { tab = q; break; }
        if (tab < len && u.szerokosc_klucza > 0) {          // wlasciwosc: dwie kolumny
            int y_klucz = y, y_wart = y;
            zawin(tekst + p, tab, u.szerokosc_klucza, mierz, [&](size_t pocz, size_t dlg) { dodaj(Typ::Klucz, 0, p + pocz, dlg, y_klucz); y_klucz += wys_linii; });
            const char* w = tekst + p + tab + 1; const size_t wl = len - tab - 1;
            zawin(w, wl, szerokosc - x_wart, mierz, [&](size_t pocz, size_t dlg) { dodaj(Typ::Wartosc, x_wart, p + tab + 1 + pocz, dlg, y_wart); y_wart += wys_linii; });
            y = (y_klucz > y_wart ? y_klucz : y_wart) + odstep / 2;
        } else {
            const bool naglowek = len >= 2 && tekst[p] == '#' && tekst[p + 1] == ' ';
            if (naglowek && !poprzedni_pusty) y += odstep;
            const size_t od = naglowek ? 2 : 0;
            zawin(tekst + p + od, len - od, szerokosc, mierz, [&](size_t pocz, size_t dlg) { dodaj(naglowek ? Typ::Naglowek : Typ::Akapit, 0, p + od + pocz, dlg, y); y += wys_linii; });
            y += odstep / 2;
        }
        poprzedni_pusty = false;
        p = k + 1;
    }
    u.wysokosc = y > 0 ? y : 0;
}

// Maksymalne przewiniecie (px) przy pelnej wysokosci tresci i wysokosci widoku.
inline int maks_przewiniecie(int wysokosc_tresci, int wysokosc_widoku) {
    return wysokosc_tresci > wysokosc_widoku && wysokosc_widoku > 0 ? wysokosc_tresci - wysokosc_widoku : 0;
}

} // namespace TrescUklad
