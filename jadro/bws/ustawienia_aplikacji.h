#pragma once
/*
 * Prywatne ustawienia aplikacji (PRAWO_USTAWIENIA_APLIKACJI).
 *
 * Model: aplikacja z tym prawem moze tworzyc/zapisywac/usuwac pliki WYLACZNIE w swoim katalogu
 *   /uzytkownicy/<uzytkownik>/Ustawienia/<id-aplikacji>/...
 * gdzie <id-aplikacji> to nazwa katalogu pakietu, z ktorego jadro uruchomilo proces
 * ("/gry/snake.cebula/snake.bur" => "snake"). Identyfikator NIE pochodzi od aplikacji ani od
 * manifestu: loader zapisuje sciezke pliku, a jadro wyprowadza z niej id.
 *
 * Dodatkowo (izolacja): zwykly proces (PZB >= UZYTKOWNIK) nie moze modyfikowac katalogu
 * ustawien INNEJ aplikacji, nawet gdy ma ogolne PRAWO_PLIKI_ZAPISZ.
 *
 * Czyste funkcje bez zaleznosci od jadra - testowane na hoscie (testy/test_ustawienia_aplikacji_host.cpp).
 */
#include <stddef.h>
#include <stdint.h>

namespace UstawieniaAplikacji {

constexpr size_t MAKS_ID = 40;

inline size_t dlugosc(const char* s, size_t limit) {
    size_t n = 0;
    while (n < limit && s[n]) ++n;
    return n;
}

inline bool znak_segmentu(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.';
}

// Segment: niepusty, dozwolone znaki, nie "." ani "..".
inline bool segment_poprawny(const char* s, size_t n) {
    if (n == 0 || n >= MAKS_ID) return false;
    if (n == 1 && s[0] == '.') return false;
    if (n == 2 && s[0] == '.' && s[1] == '.') return false;
    for (size_t i = 0; i < n; ++i) if (!znak_segmentu(s[i])) return false;
    return true;
}

// "/gry/snake.cebula/snake.bur" albo "/programy/x.cebula/x.bur" => id "snake" / "x".
// Inne sciezki (np. proces spoza paczek systemowych) nie maja katalogu ustawien.
inline bool id_z_sciezki_procesu(const char* sciezka, char* id, size_t cap) {
    if (!sciezka || !id || cap < 2) return false;
    static const char* const PREFIKSY[] = {"/gry/", "/programy/"};
    for (const char* pref : PREFIKSY) {
        size_t p = 0;
        while (pref[p] && sciezka[p] == pref[p]) ++p;
        if (pref[p]) continue;
        size_t start = p, i = p;
        while (sciezka[i] && sciezka[i] != '/') ++i;
        if (sciezka[i] != '/') return false;
        size_t len = i - start;
        static const char SUF[] = ".cebula";
        const size_t sl = sizeof(SUF) - 1;
        if (len <= sl) return false;
        for (size_t k = 0; k < sl; ++k) if (sciezka[start + len - sl + k] != SUF[k]) return false;
        len -= sl;
        if (!segment_poprawny(sciezka + start, len) || len + 1 > cap) return false;
        for (size_t k = 0; k < len; ++k) id[k] = sciezka[start + k];
        id[len] = '\0';
        return true;
    }
    return false;
}

enum class Strefa : uint8_t {
    PozaUstawieniami,     // sciezka nie dotyczy /uzytkownicy/<u>/Ustawienia...
    KatalogUstawien,      // /uzytkownicy/<u>/Ustawienia (sam katalog)
    KatalogAplikacji,     // /uzytkownicy/<u>/Ustawienia/<id> (z zawartoscia); wlasciciel w `id`
};

// Klasyfikacja sciezki KANONICZNEJ (bez ".", "..", "//"). Przy niepoprawnym segmencie => PozaUstawieniami.
inline Strefa klasyfikuj(const char* sciezka, char* id, size_t cap) {
    static const char PRE[] = "/uzytkownicy/";
    if (!sciezka) return Strefa::PozaUstawieniami;
    size_t i = 0;
    while (PRE[i] && sciezka[i] == PRE[i]) ++i;
    if (PRE[i]) return Strefa::PozaUstawieniami;
    size_t u0 = i;
    while (sciezka[i] && sciezka[i] != '/') ++i;
    if (!segment_poprawny(sciezka + u0, i - u0) || sciezka[i] != '/') return Strefa::PozaUstawieniami;
    ++i;
    static const char UST[] = "Ustawienia";
    size_t k = 0;
    while (UST[k] && sciezka[i + k] == UST[k]) ++k;
    if (UST[k]) return Strefa::PozaUstawieniami;
    i += k;
    if (sciezka[i] == '\0') return Strefa::KatalogUstawien;
    if (sciezka[i] != '/') return Strefa::PozaUstawieniami;
    ++i;
    size_t a0 = i;
    while (sciezka[i] && sciezka[i] != '/') ++i;
    const size_t len = i - a0;
    if (!segment_poprawny(sciezka + a0, len)) return Strefa::PozaUstawieniami;
    if (id) {
        if (len + 1 > cap) return Strefa::PozaUstawieniami;
        for (size_t j = 0; j < len; ++j) id[j] = sciezka[a0 + j];
        id[len] = '\0';
    }
    return Strefa::KatalogAplikacji;
}

inline bool id_rowne(const char* a, const char* b) {
    size_t i = 0;
    while (a[i] && b[i] && a[i] == b[i]) ++i;
    return a[i] == b[i];
}

enum class Decyzja : uint8_t { BrakReguly, Zezwol, Odmow };

/*
 * Reguly dla zwyklego procesu (PZB >= UZYTKOWNIK):
 *  - sciezka w katalogu WLASNEJ aplikacji: Zezwol (wystarczy PRAWO_USTAWIENIA_APLIKACJI lub PLIKI_ZAPISZ);
 *  - sciezka w katalogu INNEJ aplikacji: Odmow (takze przy PRAWO_PLIKI_ZAPISZ);
 *  - sam katalog Ustawienia: BrakReguly dla PLIKI_ZAPISZ, Odmow gdy proces ma tylko USTAWIENIA_APLIKACJI;
 *  - poza ustawieniami: BrakReguly dla PLIKI_ZAPISZ, Odmow gdy proces ma tylko USTAWIENIA_APLIKACJI.
 */
inline Decyzja ocen(const char* sciezka_procesu, const char* sciezka, bool ma_zapisz, bool ma_ustawienia_aplikacji) {
    char id_sciezki[MAKS_ID] = {};
    const Strefa s = klasyfikuj(sciezka, id_sciezki, sizeof(id_sciezki));
    char moje[MAKS_ID] = {};
    const bool mam_id = id_z_sciezki_procesu(sciezka_procesu, moje, sizeof(moje));
    if (s == Strefa::KatalogAplikacji) {
        if (!mam_id || !id_rowne(id_sciezki, moje)) return Decyzja::Odmow;
        return (ma_zapisz || ma_ustawienia_aplikacji) ? Decyzja::Zezwol : Decyzja::Odmow;
    }
    if (ma_zapisz) return Decyzja::BrakReguly;
    return Decyzja::Odmow;
}

} // namespace UstawieniaAplikacji
