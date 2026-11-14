#pragma once
/*
 * Przegladarka Obrazow - model galerii i rozpoznawanie formatow (czysta logika, bez I/O).
 *
 * Galeria to stabilna lista nazw plikow jednego katalogu (domyslnie /dane/zdjecia),
 * zbudowana RAZ z listingu katalogu BWS ("[PLIK] nazwa" / "[KAT]  nazwa"). Nawigacja
 * Poprzednie/Nastepne operuje na tej liscie i nie skanuje katalogu ponownie.
 * Kolejnosc: naturalna, bez rozrozniania wielkosci liter ASCII ("zdjecie2" < "zdjecie10").
 * Nawigacja zawija sie (po ostatnim jest pierwszy) - zachowanie jest opcjonalne (zawijaj).
 */
#include <stddef.h>
#include <stdint.h>

namespace Galeria {

enum class FormatPliku : uint8_t { Nieznany, BMP, PNG, JPEG, BTP };

inline const char* nazwa_formatu(FormatPliku f) {
    switch (f) {
        case FormatPliku::BMP: return "BMP";
        case FormatPliku::PNG: return "PNG";
        case FormatPliku::JPEG: return "JPEG";
        case FormatPliku::BTP: return "BTP";
        default: return "?";
    }
}

inline char male(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c; }

// Format wg rozszerzenia (bez rozrozniania wielkosci liter): .png .jpg .jpeg .btp .bmp
inline FormatPliku format_z_rozszerzenia(const char* nazwa) {
    if (!nazwa) return FormatPliku::Nieznany;
    size_t n = 0;
    while (nazwa[n]) ++n;
    size_t kropka = n;
    while (kropka > 0 && nazwa[kropka - 1] != '.') --kropka;
    if (kropka == 0 || kropka == n) return FormatPliku::Nieznany;     // brak kropki albo kropka na koncu
    const char* e = nazwa + kropka;
    const size_t el = n - kropka;
    auto rowne = [&](const char* w) {
        size_t i = 0;
        for (; w[i]; ++i) if (i >= el || male(e[i]) != w[i]) return false;
        return i == el;
    };
    if (rowne("png")) return FormatPliku::PNG;
    if (rowne("jpg") || rowne("jpeg")) return FormatPliku::JPEG;
    if (rowne("btp")) return FormatPliku::BTP;
    if (rowne("bmp")) return FormatPliku::BMP;
    return FormatPliku::Nieznany;
}

// Porownanie naturalne: ciagi cyfr jako liczby (bez przepelnien: porownanie dlugosci po
// pominieciu zer wiodacych), reszta bez rozrozniania wielkosci ASCII, remis => bajtowo.
inline int porownaj_naturalnie(const char* a, const char* b) {
    const char* pa = a; const char* pb = b;
    while (*pa && *pb) {
        const bool da = *pa >= '0' && *pa <= '9', db = *pb >= '0' && *pb <= '9';
        if (da && db) {
            const char* za = pa; const char* zb = pb;
            while (*za == '0') ++za;
            while (*zb == '0') ++zb;
            const char* ea = za; const char* eb = zb;
            while (*ea >= '0' && *ea <= '9') ++ea;
            while (*eb >= '0' && *eb <= '9') ++eb;
            const ptrdiff_t la = ea - za, lb = eb - zb;
            if (la != lb) return la < lb ? -1 : 1;
            for (; za < ea; ++za, ++zb) if (*za != *zb) return *za < *zb ? -1 : 1;
            pa = ea; pb = eb;
            continue;
        }
        const char ca = male(*pa), cb = male(*pb);
        if (ca != cb) return static_cast<unsigned char>(ca) < static_cast<unsigned char>(cb) ? -1 : 1;
        ++pa; ++pb;
    }
    if (*pa) return 1;
    if (*pb) return -1;
    // identyczne wg porownania naturalnego: porzadek deterministyczny po surowych bajtach
    for (; *a && *b; ++a, ++b) if (*a != *b) return static_cast<unsigned char>(*a) < static_cast<unsigned char>(*b) ? -1 : 1;
    return 0;
}

struct ModelGalerii {
    static constexpr int MAKS_OBRAZOW = 256;
    static constexpr size_t MAKS_NAZWY = 64;
    char katalog[128] = {};
    char nazwy[MAKS_OBRAZOW][MAKS_NAZWY] = {};
    int liczba = 0;
    int biezacy = -1;
    bool zawijaj = true;
    bool przyciete = false;        // w katalogu bylo wiecej obrazow niz MAKS_OBRAZOW

    void wyczysc() { liczba = 0; biezacy = -1; przyciete = false; }

    // listing BWS 6: wiersze "[PLIK] nazwa\n" / "[KAT]  nazwa\n" (prefiks 7 znakow). Katalogi i pliki
    // o nieobslugiwanym rozszerzeniu sa pomijane. Zwraca liczbe obrazow.
    int zbuduj(const char* kat, const char* listing, size_t dlugosc) {
        wyczysc();
        size_t k = 0;
        while (kat && kat[k] && k + 1 < sizeof(katalog)) { katalog[k] = kat[k]; ++k; }
        katalog[k] = '\0';
        if (!listing) return 0;
        size_t pos = 0;
        while (pos < dlugosc && listing[pos]) {
            size_t end = pos;
            while (end < dlugosc && listing[end] && listing[end] != '\n') ++end;
            if (end - pos > 7 && listing[pos] == '[' && listing[pos + 1] == 'P') {
                const size_t nl = end - (pos + 7);
                if (nl > 0 && nl < MAKS_NAZWY) {
                    char nazwa[MAKS_NAZWY];
                    for (size_t i = 0; i < nl; ++i) nazwa[i] = listing[pos + 7 + i];
                    nazwa[nl] = '\0';
                    if (format_z_rozszerzenia(nazwa) != FormatPliku::Nieznany) {
                        if (liczba < MAKS_OBRAZOW) dodaj_posortowany(nazwa);
                        else przyciete = true;
                    }
                }
            }
            pos = (end < dlugosc && listing[end] == '\n') ? end + 1 : end;
        }
        biezacy = liczba > 0 ? 0 : -1;
        return liczba;
    }

    void dodaj_posortowany(const char* nazwa) {
        int i = liczba;
        while (i > 0 && porownaj_naturalnie(nazwy[i - 1], nazwa) > 0) {
            for (size_t j = 0; j < MAKS_NAZWY; ++j) nazwy[i][j] = nazwy[i - 1][j];
            --i;
        }
        size_t j = 0;
        for (; nazwa[j] && j + 1 < MAKS_NAZWY; ++j) nazwy[i][j] = nazwa[j];
        nazwy[i][j] = '\0';
        ++liczba;
    }

    bool mozna_nawigowac() const { return liczba >= 2; }

    // Zwraca nowy indeks albo -1 (brak ruchu: <2 obrazy albo koniec listy bez zawijania).
    int nastepny() const {
        if (!mozna_nawigowac() || biezacy < 0) return -1;
        if (biezacy + 1 < liczba) return biezacy + 1;
        return zawijaj ? 0 : -1;
    }
    int poprzedni() const {
        if (!mozna_nawigowac() || biezacy < 0) return -1;
        if (biezacy > 0) return biezacy - 1;
        return zawijaj ? liczba - 1 : -1;
    }

    int znajdz(const char* nazwa) const {
        for (int i = 0; i < liczba; ++i) {
            size_t k = 0;
            while (nazwy[i][k] && nazwa[k] && nazwy[i][k] == nazwa[k]) ++k;
            if (!nazwy[i][k] && !nazwa[k]) return i;
        }
        return -1;
    }

    bool pelna_sciezka(int i, char* out, size_t cap) const {
        if (i < 0 || i >= liczba || !out) return false;
        size_t n = 0;
        for (size_t k = 0; katalog[k]; ++k) { if (n + 1 >= cap) return false; out[n++] = katalog[k]; }
        if (n == 0 || out[n - 1] != '/') { if (n + 1 >= cap) return false; out[n++] = '/'; }
        for (size_t k = 0; nazwy[i][k]; ++k) { if (n + 1 >= cap) return false; out[n++] = nazwy[i][k]; }
        out[n] = '\0';
        return true;
    }
};

} // namespace Galeria
