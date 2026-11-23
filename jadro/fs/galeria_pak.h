#pragma once
/*
 * Kontener galerii "BZP3" (modul Multiboot2 zdjecia.pak): zwarty, binarny, bez kodowania tekstowego.
 *
 *   0   "BZP3"
 *   4   u32 wersja (1)
 *   8   u32 liczba wpisow (1..256)
 *   12  u32 rozmiar tabeli (bajty)
 *   16  u8[32] SHA-256 tabeli (integralnosc metadanych: nazwy, rozmiary, przesuniecia, CRC)
 *   48  tabela: dla kazdego wpisu  u8 dl_nazwy (1..63), nazwa UTF-8, u32 rozmiar, u32 przesuniecie (od poczatku
 *       kontenera), u32 CRC-32 danych
 *   ... dane wpisow: kolejno, bez luk i nakladania, od konca tabeli do konca kontenera
 *
 * Wszystkie liczby little-endian. Parser jest ODPORNY na niepoprawne dane: kazda granica jest sprawdzana
 * (arytmetyka 64-bitowa), nazwy sa walidowane (brak '/', '\\', znakow sterujacych, "." i ".."), rozszerzenie musi
 * nalezec do formatow obrazow, nazwy sa unikalne (takze bez rozroznienia wielkosci liter ASCII), a przesuniecia
 * musza tworzyc ciagly, nienakladajacy sie uklad. Kontener niesie WYLACZNIE bajty plikow - jadro ich nie
 * dekoduje. Dane obrazow sa surowe (skaner wydania waliduje je wg rzeczywistego formatu, patrz
 * narzedzia/verifyreleasemetadata.py).
 *
 * Czyste funkcje bez zaleznosci od jadra: testy hosta w testy/test_galeria_pak_host.cpp.
 */
#include <stddef.h>
#include <stdint.h>

namespace GaleriaPak {

constexpr uint32_t WERSJA = 1;
constexpr uint32_t MAKS_WPISOW = 256;
constexpr uint32_t MAKS_NAZWA = 63;
constexpr uint64_t MAKS_KONTENER = 200ULL * 1024ULL * 1024ULL;
constexpr uint64_t MAKS_PLIK = 64ULL * 1024ULL * 1024ULL;
constexpr uint64_t MAKS_TABELA = MAKS_WPISOW * (1 + MAKS_NAZWA + 12);
constexpr uint64_t ROZMIAR_NAGLOWKA = 48;

struct Wpis {
    uint8_t dl;
    char nazwa[MAKS_NAZWA + 1];
    uint32_t rozmiar;
    uint32_t przesuniecie;
    uint32_t crc;
};

enum class Blad : uint8_t {
    OK, ZaMaly, Sygnatura, Wersja, Liczba, Tabela, SumaTabeli, Nazwa, Rozszerzenie, Duplikat, Zakres, Uklad, Rozmiar, Sha
};

using SkrotSha256 = bool (*)(const uint8_t* dane, uint64_t rozmiar, uint8_t wynik[32]);

inline uint32_t le32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

// CRC-32 (IEEE 802.3), strumieniowo: crc = crc32(dane, n, crc_poprzedni); start 0.
inline uint32_t crc32(const uint8_t* d, uint64_t n, uint32_t crc = 0) {
    crc = ~crc;
    for (uint64_t i = 0; i < n; ++i) {
        crc ^= d[i];
        for (int b = 0; b < 8; ++b) crc = (crc >> 1) ^ (0xEDB88320U & (0U - (crc & 1U)));
    }
    return ~crc;
}

inline char male(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

inline bool nazwa_poprawna(const uint8_t* n, uint32_t dl) {
    if (!n || dl == 0 || dl > MAKS_NAZWA) return false;
    if (dl == 1 && n[0] == '.') return false;
    if (dl == 2 && n[0] == '.' && n[1] == '.') return false;
    for (uint32_t i = 0; i < dl; ++i) {
        const uint8_t c = n[i];
        if (c < 0x20 || c == 0x7F || c == '/' || c == '\\' || c == '\0') return false;
    }
    return true;
}

inline bool rozszerzenie_obrazu(const uint8_t* n, uint32_t dl) {
    uint32_t kropka = dl;
    for (uint32_t i = 0; i < dl; ++i) if (n[i] == '.') kropka = i;
    if (kropka == dl || kropka == 0) return false;
    char e[8] = {};
    const uint32_t el = dl - kropka - 1;
    if (el == 0 || el > 4) return false;
    for (uint32_t i = 0; i < el; ++i) e[i] = male(static_cast<char>(n[kropka + 1 + i]));
    auto rowne = [&](const char* s) { uint32_t i = 0; while (s[i] && s[i] == e[i]) ++i; return s[i] == '\0' && e[i] == '\0'; };
    return rowne("png") || rowne("jpg") || rowne("jpeg") || rowne("btp") || rowne("bmp");
}

// Muzyka (modul muzyka.pak, ten sam kontener BZP3): tylko formaty odtwarzane przez Grajka 1.0.
inline bool rozszerzenie_audio(const uint8_t* n, uint32_t dl) {
    uint32_t kropka = dl;
    for (uint32_t i = 0; i < dl; ++i) if (n[i] == '.') kropka = i;
    if (kropka == dl || kropka == 0 || dl - kropka - 1 != 3) return false;
    const char a = male(static_cast<char>(n[kropka + 1])), b = male(static_cast<char>(n[kropka + 2])), c = male(static_cast<char>(n[kropka + 3]));
    return (a == 'w' && b == 'a' && c == 'v') || (a == 'm' && b == 'p' && c == '3');
}
using PredykatRozszerzenia = bool (*)(const uint8_t* nazwa, uint32_t dl);

inline bool nazwy_rowne_ci(const Wpis& a, const Wpis& b) {
    if (a.dl != b.dl) return false;
    for (uint32_t i = 0; i < a.dl; ++i) if (male(a.nazwa[i]) != male(b.nazwa[i])) return false;
    return true;
}

/*
 * Waliduje kontener i wypelnia `wpisy` (pojemnosc MAKS_WPISOW). Nie czyta danych wpisow
 * (CRC danych sprawdza wolajacy przy kopiowaniu).
 */
inline Blad parsuj(const uint8_t* p, uint64_t n, Wpis* wpisy, uint32_t* liczba_out, SkrotSha256 sha,
                  PredykatRozszerzenia rozszerzenie_ok = rozszerzenie_obrazu) {
    if (liczba_out) *liczba_out = 0;
    if (!p || !wpisy || !sha) return Blad::ZaMaly;
    if (n < ROZMIAR_NAGLOWKA) return Blad::ZaMaly;
    if (n > MAKS_KONTENER) return Blad::Rozmiar;
    if (p[0] != 'B' || p[1] != 'Z' || p[2] != 'P' || p[3] != '3') return Blad::Sygnatura;
    if (le32(p + 4) != WERSJA) return Blad::Wersja;
    const uint32_t liczba = le32(p + 8);
    if (liczba == 0 || liczba > MAKS_WPISOW) return Blad::Liczba;
    const uint64_t tabela = le32(p + 12);
    if (tabela < static_cast<uint64_t>(liczba) * (1 + 1 + 12) || tabela > MAKS_TABELA) return Blad::Tabela;
    if (ROZMIAR_NAGLOWKA + tabela > n) return Blad::Tabela;
    uint8_t skrot[32];
    if (!sha(p + ROZMIAR_NAGLOWKA, tabela, skrot)) return Blad::Sha;
    uint8_t roznica = 0;
    for (int i = 0; i < 32; ++i) roznica |= static_cast<uint8_t>(skrot[i] ^ p[16 + i]);
    if (roznica) return Blad::SumaTabeli;

    const uint8_t* t = p + ROZMIAR_NAGLOWKA;
    uint64_t poz = 0;
    uint64_t oczekiwane = ROZMIAR_NAGLOWKA + tabela;      // pierwszy bajt danych
    for (uint32_t i = 0; i < liczba; ++i) {
        if (poz + 1 > tabela) return Blad::Tabela;
        const uint32_t dl = t[poz++];
        if (dl == 0 || dl > MAKS_NAZWA || poz + dl + 12 > tabela) return Blad::Tabela;
        Wpis& w = wpisy[i];
        w.dl = static_cast<uint8_t>(dl);
        for (uint32_t k = 0; k < dl; ++k) w.nazwa[k] = static_cast<char>(t[poz + k]);
        w.nazwa[dl] = '\0';
        poz += dl;
        if (!nazwa_poprawna(t + poz - dl, dl)) return Blad::Nazwa;
        if (!rozszerzenie_ok || !rozszerzenie_ok(t + poz - dl, dl)) return Blad::Rozszerzenie;
        w.rozmiar = le32(t + poz);
        w.przesuniecie = le32(t + poz + 4);
        w.crc = le32(t + poz + 8);
        poz += 12;
        for (uint32_t j = 0; j < i; ++j) if (nazwy_rowne_ci(wpisy[j], w)) return Blad::Duplikat;
        if (w.rozmiar == 0 || w.rozmiar > MAKS_PLIK) return Blad::Rozmiar;
        if (w.przesuniecie != oczekiwane) return Blad::Uklad;                 // ciagly uklad, bez luk i nakladania
        if (oczekiwane + w.rozmiar > n) return Blad::Zakres;
        oczekiwane += w.rozmiar;
    }
    if (poz != tabela) return Blad::Tabela;                                    // tabela zuzyta w calosci
    if (oczekiwane != n) return Blad::Uklad;                                   // brak "ogona" po ostatnim wpisie
    if (liczba_out) *liczba_out = liczba;
    return Blad::OK;
}

inline const char* opis(Blad b) {
    switch (b) {
        case Blad::OK: return "ok";
        case Blad::ZaMaly: return "za maly";
        case Blad::Sygnatura: return "zla sygnatura";
        case Blad::Wersja: return "zla wersja";
        case Blad::Liczba: return "zla liczba wpisow";
        case Blad::Tabela: return "uszkodzona tabela";
        case Blad::SumaTabeli: return "suma SHA-256 tabeli";
        case Blad::Nazwa: return "niepoprawna nazwa";
        case Blad::Rozszerzenie: return "niedozwolone rozszerzenie";
        case Blad::Duplikat: return "duplikat nazwy";
        case Blad::Zakres: return "wpis poza kontenerem";
        case Blad::Uklad: return "nieciagly uklad danych";
        case Blad::Rozmiar: return "niedozwolony rozmiar";
        case Blad::Sha: return "blad SHA-256";
    }
    return "?";
}

} // namespace GaleriaPak
