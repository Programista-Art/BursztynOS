#pragma once
#include <stdint.h>
#include <stddef.h>

/*
 * tapety_nazwy.h - Wspolny modul kanonicznych nazw i walidacji tapet dla Bursztyn OS.
 *
 * Architektura dwoch nazw:
 * 1. Nazwa wyswietlana (Display Name):
 *    Dokladna zrodlowa nazwa pliku w UTF-8 (np. "Żółta łąka.jpg", "morze-zachód.jpeg").
 *    Prezentowana uzytkownikowi w GUI bez zastepowania polskich znakow i bez sanitizacji ASCII.
 *
 * 2. Nazwa kanoniczna (Canonical Internal Name):
 *    Deterministyczny, bezpieczny identyfikator ASCII uzywany wewnetrznie przez jadro,
 *    system plikow PSF/BSP2 (/tapety/<kanoniczna>) i konfiguracje (/ustawienia/pulpit.cfg).
 *    Dla wbudowanych tapet systemowych "bursztyn01.btp".."bursztyn10.btp" zachowuje oryginalna nazwe.
 *    Dla wszystkich innych tapet: "wp_<16_hex_fnv1a>.<rozszerzenie>", np. "wp_a1b2c3d4e5f60718.jpg".
 *    Alfabet: scisle [a-z0-9_.], dlugosc <= 24 bajty (mieszcza sie w PSF_MAX_NAZWA = 56).
 */

inline bool tapeta_czy_bursztyn_systemowa(const char* nazwa) {
    if (!nazwa) return false;
    // Format: "bursztyn01.btp" .. "bursztyn10.btp"
    if (nazwa[0] != 'b' && nazwa[0] != 'B') return false;
    if (nazwa[1] != 'u' && nazwa[1] != 'U') return false;
    if (nazwa[2] != 'r' && nazwa[2] != 'R') return false;
    if (nazwa[3] != 's' && nazwa[3] != 'S') return false;
    if (nazwa[4] != 'z' && nazwa[4] != 'Z') return false;
    if (nazwa[5] != 't' && nazwa[5] != 'T') return false;
    if (nazwa[6] != 'y' && nazwa[6] != 'Y') return false;
    if (nazwa[7] != 'n' && nazwa[7] != 'N') return false;
    if (nazwa[8] == '0' && nazwa[9] >= '1' && nazwa[9] <= '9') {
        const char* ext = nazwa + 10;
        return (ext[0] == '.' && (ext[1] == 'b' || ext[1] == 'B') &&
                (ext[2] == 't' || ext[2] == 'T') && (ext[3] == 'p' || ext[3] == 'P') && ext[4] == '\0');
    }
    if (nazwa[8] == '1' && nazwa[9] == '0') {
        const char* ext = nazwa + 10;
        return (ext[0] == '.' && (ext[1] == 'b' || ext[1] == 'B') &&
                (ext[2] == 't' || ext[2] == 'T') && (ext[3] == 'p' || ext[3] == 'P') && ext[4] == '\0');
    }
    return false;
}

inline bool tapeta_sprawdz_rozszerzenie(const char* nazwa, char* out_ext_lowercase = nullptr, size_t ext_cap = 0) {
    if (!nazwa) return false;
    size_t len = 0;
    while (nazwa[len]) ++len;
    if (len < 5) return false;

    auto to_lower = [](char c) -> char {
        return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + ('a' - 'A')) : c;
    };

    if (len >= 4 && nazwa[len - 4] == '.') {
        char e1 = to_lower(nazwa[len - 3]);
        char e2 = to_lower(nazwa[len - 2]);
        char e3 = to_lower(nazwa[len - 1]);
        if (e1 == 'b' && e2 == 't' && e3 == 'p') {
            if (out_ext_lowercase && ext_cap >= 4) {
                out_ext_lowercase[0] = 'b'; out_ext_lowercase[1] = 't'; out_ext_lowercase[2] = 'p'; out_ext_lowercase[3] = '\0';
            }
            return true;
        }
        if (e1 == 'j' && e2 == 'p' && e3 == 'g') {
            if (out_ext_lowercase && ext_cap >= 4) {
                out_ext_lowercase[0] = 'j'; out_ext_lowercase[1] = 'p'; out_ext_lowercase[2] = 'g'; out_ext_lowercase[3] = '\0';
            }
            return true;
        }
        if (e1 == 'p' && e2 == 'n' && e3 == 'g') {
            if (out_ext_lowercase && ext_cap >= 4) {
                out_ext_lowercase[0] = 'p'; out_ext_lowercase[1] = 'n'; out_ext_lowercase[2] = 'g'; out_ext_lowercase[3] = '\0';
            }
            return true;
        }
    }
    if (len >= 5 && nazwa[len - 5] == '.') {
        char e1 = to_lower(nazwa[len - 4]);
        char e2 = to_lower(nazwa[len - 3]);
        char e3 = to_lower(nazwa[len - 2]);
        char e4 = to_lower(nazwa[len - 1]);
        if (e1 == 'j' && e2 == 'p' && e3 == 'e' && e4 == 'g') {
            if (out_ext_lowercase && ext_cap >= 5) {
                out_ext_lowercase[0] = 'j'; out_ext_lowercase[1] = 'p'; out_ext_lowercase[2] = 'e'; out_ext_lowercase[3] = 'g'; out_ext_lowercase[4] = '\0';
            }
            return true;
        }
    }
    return false;
}

inline bool tapeta_sprawdz_nazwe_utf8(const char* nazwa) {
    if (!nazwa || !nazwa[0] || nazwa[0] == '.') return false;
    size_t len = 0;
    while (nazwa[len]) {
        const char c = nazwa[len];
        if (c == '/' || c == '\\' || c == '\r' || c == '\n' || c == '"') return false;
        if (len >= 1 && c == '.' && nazwa[len - 1] == '.') return false;
        ++len;
        if (len > 120) return false;
    }
    if (len < 5) return false;

    // Sprawdz poprawnosc sekwencji UTF-8
    size_t i = 0;
    while (i < len) {
        const uint8_t c = static_cast<uint8_t>(nazwa[i]);
        if (c < 128) {
            // Bajty kontrolne ASCII zabronione
            if (c < 0x20 || c == 0x7F) return false;
            ++i;
            continue;
        }
        uint32_t cp = 0, ile = 0, minimum = 0;
        if (c >= 0xC2 && c <= 0xDF) { cp = c & 31U; ile = 2; minimum = 0x80; }
        else if (c >= 0xE0 && c <= 0xEF) { cp = c & 15U; ile = 3; minimum = 0x800; }
        else if (c >= 0xF0 && c <= 0xF4) { cp = c & 7U; ile = 4; minimum = 0x10000; }
        else return false;

        if (ile > len - i) return false;
        for (uint32_t j = 1; j < ile; ++j) {
            const uint8_t d = static_cast<uint8_t>(nazwa[i + j]);
            if ((d & 0xC0) != 0x80) return false;
            cp = (cp << 6) | (d & 63U);
        }
        if (cp < minimum || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return false;
        i += ile;
    }

    return tapeta_sprawdz_rozszerzenie(nazwa);
}

inline uint64_t tapeta_fnv1a64(const uint8_t* dane, size_t dlugosc) {
    if (!dane) return 0;
    uint64_t h = 0xcbf29ce484222325ULL;
    for (size_t i = 0; i < dlugosc; ++i) {
        h ^= static_cast<uint64_t>(dane[i]);
        h *= 0x100000001b3ULL;
    }
    return h;
}

inline bool tapeta_kanoniczna_nazwa(const char* display_name, char* out_canonical, size_t out_cap) {
    if (!display_name || !out_canonical || out_cap < 28) return false;
    if (!tapeta_sprawdz_nazwe_utf8(display_name)) return false;

    if (tapeta_czy_bursztyn_systemowa(display_name)) {
        // Zachowaj dokladnie bursztynXX.btp w lowercase
        size_t len = 0;
        while (display_name[len]) ++len;
        if (len + 1 > out_cap) return false;
        for (size_t i = 0; i < len; ++i) {
            char c = display_name[i];
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
            out_canonical[i] = c;
        }
        out_canonical[len] = '\0';
        return true;
    }

    char ext[8]{};
    if (!tapeta_sprawdz_rozszerzenie(display_name, ext, sizeof(ext))) return false;

    size_t name_len = 0;
    while (display_name[name_len]) ++name_len;

    const uint64_t h = tapeta_fnv1a64(reinterpret_cast<const uint8_t*>(display_name), name_len);

    // Format: wp_<16 hex digits>.<ext>
    // Dlugosc: 3 + 16 + 1 + len(ext) + 1 <= 25 bajtow
    constexpr char hex[] = "0123456789abcdef";
    out_canonical[0] = 'w';
    out_canonical[1] = 'p';
    out_canonical[2] = '_';
    for (int b = 15; b >= 0; --b) {
        uint8_t nib = static_cast<uint8_t>((h >> (b * 4)) & 0x0FU);
        out_canonical[3 + (15 - b)] = hex[nib];
    }
    size_t pos = 19;
    out_canonical[pos++] = '.';
    for (size_t k = 0; ext[k] && pos + 1 < out_cap; ++k) {
        out_canonical[pos++] = ext[k];
    }
    out_canonical[pos] = '\0';
    return true;
}

inline bool tapeta_kanoniczna_sciezka(const char* display_name, char* out_path, size_t out_cap) {
    if (!out_path || out_cap < 36) return false;
    constexpr char prefix[] = "/tapety/";
    size_t p = 0;
    while (prefix[p] && p + 1 < out_cap) {
        out_path[p] = prefix[p];
        ++p;
    }
    out_path[p] = '\0';
    return tapeta_kanoniczna_nazwa(display_name, out_path + p, out_cap - p);
}

inline bool tapeta_czy_kanoniczna_nazwa(const char* nazwa) {
    if (!nazwa) return false;
    if (tapeta_czy_bursztyn_systemowa(nazwa)) return true;
    if (nazwa[0] != 'w' || nazwa[1] != 'p' || nazwa[2] != '_') return false;
    for (int i = 0; i < 16; ++i) {
        char c = nazwa[3 + i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    }
    const char* ext = nazwa + 19;
    if (ext[0] != '.') return false;
    if (ext[1] == 'j' && ext[2] == 'p' && ext[3] == 'g' && ext[4] == '\0') return true;
    if (ext[1] == 'b' && ext[2] == 't' && ext[3] == 'p' && ext[4] == '\0') return true;
    if (ext[1] == 'p' && ext[2] == 'n' && ext[3] == 'g' && ext[4] == '\0') return true;
    if (ext[1] == 'j' && ext[2] == 'p' && ext[3] == 'e' && ext[4] == 'g' && ext[5] == '\0') return true;
    return false;
}

inline bool tapeta_czy_kanoniczna_sciezka(const char* sciezka) {
    if (!sciezka) return false;
    constexpr char prefix[] = "/tapety/";
    for (size_t i = 0; prefix[i]; ++i) {
        if (sciezka[i] != prefix[i]) return false;
    }
    return tapeta_czy_kanoniczna_nazwa(sciezka + 8);
}
