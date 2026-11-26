#pragma once
/*
 * Grajek - ograniczone operacje na tekscie UTF-8 (bez libc, bez alokacji).
 *
 * Wszystkie funkcje przyjmuja pojemnosc bufora docelowego i ZAWSZE koncza wynik znakiem NUL. Obcinanie nigdy nie
 * przecina wielobajtowego znaku UTF-8. Dane wejsciowe (nazwy plikow, tagi ID3/RIFF, listy M3U8) sa traktowane jako
 * niezaufane: niepoprawne sekwencje UTF-8 i znaki sterujace sa zastepowane znakiem '?'.
 */
#include <stddef.h>
#include <stdint.h>

namespace Grajek {

inline size_t dlugosc(const char* s, size_t limit = 4096) {
    size_t n = 0;
    if (!s) return 0;
    while (n < limit && s[n]) ++n;
    return n;
}

inline bool rowne(const char* a, const char* b) {
    if (!a || !b) return false;
    size_t i = 0;
    for (; a[i] && b[i]; ++i) if (a[i] != b[i]) return false;
    return a[i] == b[i];
}

inline bool zaczyna_sie(const char* s, const char* prefiks) {
    if (!s || !prefiks) return false;
    for (size_t i = 0; prefiks[i]; ++i) if (s[i] != prefiks[i]) return false;
    return true;
}

inline void zeruj(void* p, size_t n) {
    auto* d = static_cast<volatile uint8_t*>(p);
    for (size_t i = 0; i < n; ++i) d[i] = 0;
}

inline void kopiuj_bajty(void* dst, const void* src, size_t n) {
    auto* d = static_cast<uint8_t*>(dst);
    const auto* s = static_cast<const uint8_t*>(src);
    for (size_t i = 0; i < n; ++i) d[i] = s[i];
}

// Dlugosc poprawnej sekwencji UTF-8 zaczynajacej sie w s[0..n) albo 0 (sekwencja niepoprawna / niepelna).
// Odrzuca formy nadmiarowe, surogaty i kody > U+10FFFF.
inline size_t sekwencja_utf8(const uint8_t* s, size_t n, uint32_t* kod = nullptr) {
    if (!n) return 0;
    const uint8_t c = s[0];
    if (c < 0x80) { if (kod) *kod = c; return 1; }
    size_t len; uint32_t cp, min;
    if ((c & 0xE0) == 0xC0) { len = 2; cp = c & 0x1F; min = 0x80; }
    else if ((c & 0xF0) == 0xE0) { len = 3; cp = c & 0x0F; min = 0x800; }
    else if ((c & 0xF8) == 0xF0) { len = 4; cp = c & 0x07; min = 0x10000; }
    else return 0;
    if (n < len) return 0;
    for (size_t i = 1; i < len; ++i) {
        if ((s[i] & 0xC0) != 0x80) return 0;
        cp = (cp << 6) | (s[i] & 0x3F);
    }
    if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return 0;
    if (kod) *kod = cp;
    return len;
}

// Koduje punkt kodowy do UTF-8. Zwraca liczbe bajtow (0 = brak miejsca albo niepoprawny kod).
inline size_t zakoduj_utf8(uint32_t cp, char* out, size_t miejsce) {
    if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) cp = '?';
    if (cp < 0x80) { if (miejsce < 1) return 0; out[0] = char(cp); return 1; }
    if (cp < 0x800) {
        if (miejsce < 2) return 0;
        out[0] = char(0xC0 | (cp >> 6)); out[1] = char(0x80 | (cp & 0x3F)); return 2;
    }
    if (cp < 0x10000) {
        if (miejsce < 3) return 0;
        out[0] = char(0xE0 | (cp >> 12)); out[1] = char(0x80 | ((cp >> 6) & 0x3F)); out[2] = char(0x80 | (cp & 0x3F));
        return 3;
    }
    if (miejsce < 4) return 0;
    out[0] = char(0xF0 | (cp >> 18)); out[1] = char(0x80 | ((cp >> 12) & 0x3F));
    out[2] = char(0x80 | ((cp >> 6) & 0x3F)); out[3] = char(0x80 | (cp & 0x3F));
    return 4;
}

/*
 * Budowniczy tekstu w buforze o stalej pojemnosci. Nadmiar jest obcinany na granicy znaku UTF-8.
 * Znaki sterujace (< 0x20, 0x7F) i niepoprawne sekwencje z dodaj_utf8() zamieniane sa na '?'.
 */
class Tekst {
public:
    Tekst(char* bufor, size_t pojemnosc) : b_(bufor), cap_(pojemnosc) { if (b_ && cap_) b_[0] = '\0'; }
    size_t dlugosc() const { return n_; }
    bool obciety() const { return obciety_; }
    const char* c_str() const { return b_; }

    Tekst& znak_kodu(uint32_t cp) {
        if (!b_ || cap_ == 0) return *this;
        if (cp < 0x20 || cp == 0x7F) cp = '?';
        char tmp[4];
        const size_t k = zakoduj_utf8(cp, tmp, sizeof(tmp));
        if (!k || n_ + k + 1 > cap_) { obciety_ = true; return *this; }
        for (size_t i = 0; i < k; ++i) b_[n_++] = tmp[i];
        b_[n_] = '\0';
        return *this;
    }
    // Jawny koniec wiersza (dodaj() zamienia znaki sterujace, w tym '\n', na '?').
    Tekst& nowa_linia() {
        if (!b_ || cap_ == 0) return *this;
        if (n_ + 2 > cap_) { obciety_ = true; return *this; }
        b_[n_++] = '\n';
        b_[n_] = '\0';
        return *this;
    }
    // Dolacza tekst zbudowany wczesniej przez inny Tekst (juz zwalidowany, moze zawierac '\n'). Calosc albo nic.
    Tekst& dolacz(const Tekst& inny) {
        if (!b_ || !inny.b_ || n_ + inny.n_ + 1 > cap_) { obciety_ = true; return *this; }
        for (size_t i = 0; i < inny.n_; ++i) b_[n_++] = inny.b_[i];
        b_[n_] = '\0';
        return *this;
    }
    // Tekst juz UTF-8 (np. stale z kodu albo nazwa pliku): kopiowany z walidacja.
    Tekst& dodaj(const char* s, size_t limit = 4096) {
        if (!s) return *this;
        return dodaj_utf8(reinterpret_cast<const uint8_t*>(s), Grajek::dlugosc(s, limit));
    }
    Tekst& dodaj_utf8(const uint8_t* s, size_t n) {
        size_t i = 0;
        while (i < n && !obciety_) {
            if (!s[i]) break;
            uint32_t cp = 0;
            const size_t k = sekwencja_utf8(s + i, n - i, &cp);
            if (!k) { znak_kodu('?'); ++i; continue; }
            znak_kodu(cp);
            i += k;
        }
        return *this;
    }
    Tekst& liczba(uint64_t v) {
        char t[24]; int k = 0;
        do { t[k++] = char('0' + v % 10); v /= 10; } while (v && k < 23);
        while (k) znak_kodu(uint32_t(t[--k]));
        return *this;
    }
    Tekst& liczba_ze(uint64_t v, int cyfry) {   // z zerami wiodacymi
        char t[24]; int k = 0;
        do { t[k++] = char('0' + v % 10); v /= 10; } while ((v || k < cyfry) && k < 23);
        while (k) znak_kodu(uint32_t(t[--k]));
        return *this;
    }
    // Czas w milisekundach jako m:ss albo h:mm:ss.
    Tekst& czas(uint64_t ms) {
        const uint64_t s = ms / 1000;
        const uint64_t h = s / 3600, m = (s / 60) % 60, sek = s % 60;
        if (h) { liczba(h); znak_kodu(':'); liczba_ze(m, 2); }
        else liczba(m);
        znak_kodu(':');
        return liczba_ze(sek, 2);
    }

private:
    char* b_;
    size_t cap_;
    size_t n_ = 0;
    bool obciety_ = false;
};

inline void kopiuj_tekst(char* dst, size_t cap, const char* src) { Tekst t(dst, cap); t.dodaj(src); }

// ISO-8859-1 -> UTF-8 (ID3 kodowanie 0, ID3v1, RIFF INFO bez deklaracji kodowania).
inline void latin1_do_utf8(const uint8_t* s, size_t n, char* dst, size_t cap) {
    Tekst t(dst, cap);
    for (size_t i = 0; i < n && s[i]; ++i) t.znak_kodu(s[i]);
}

// UTF-16 (z BOM albo jawnie BE/LE) -> UTF-8. Pary surogatow sa skladane, niesparowane surogaty -> '?'.
inline void utf16_do_utf8(const uint8_t* s, size_t n, bool big_endian, bool wykryj_bom, char* dst, size_t cap) {
    Tekst t(dst, cap);
    size_t i = 0;
    if (wykryj_bom && n >= 2) {
        if (s[0] == 0xFF && s[1] == 0xFE) { big_endian = false; i = 2; }
        else if (s[0] == 0xFE && s[1] == 0xFF) { big_endian = true; i = 2; }
    }
    auto jednostka = [&](size_t p) -> uint32_t {
        return big_endian ? (uint32_t(s[p]) << 8) | s[p + 1] : (uint32_t(s[p + 1]) << 8) | s[p];
    };
    while (i + 1 < n) {
        uint32_t u = jednostka(i);
        i += 2;
        if (!u) break;
        if (u >= 0xD800 && u <= 0xDBFF) {
            if (i + 1 < n) {
                const uint32_t d = jednostka(i);
                if (d >= 0xDC00 && d <= 0xDFFF) { i += 2; u = 0x10000 + ((u - 0xD800) << 10) + (d - 0xDC00); }
                else u = '?';
            } else u = '?';
        } else if (u >= 0xDC00 && u <= 0xDFFF) u = '?';
        t.znak_kodu(u);
    }
}

// Usuwa biale znaki z poczatku i konca (in situ).
inline void przytnij(char* s) {
    if (!s) return;
    size_t n = dlugosc(s), p = 0;
    while (p < n && (s[p] == ' ' || s[p] == '\t')) ++p;
    while (n > p && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' || s[n - 1] == '\n')) --n;
    size_t k = 0;
    for (size_t i = p; i < n; ++i) s[k++] = s[i];
    s[k] = '\0';
}

/*
 * Klucz porownania znaku: wielkosc liter bez znaczenia, polskie litery tuz po literze bazowej (a < ą < b),
 * dzieki czemu sortowanie jest zgodne z intuicja polskiego uzytkownika. bez_diakrytykow=true sprowadza
 * "ą" do "a" (wyszukiwanie: "zolc" znajduje "Żółć").
 */
inline uint32_t klucz_znaku(uint32_t cp, bool bez_diakrytykow = false) {
    if (cp >= 'A' && cp <= 'Z') cp += 32;
    uint32_t baza = cp, wariant = 0;
    switch (cp) {
        case 0x104: case 0x105: baza = 'a'; wariant = 1; break;   // Ą ą
        case 0x106: case 0x107: baza = 'c'; wariant = 1; break;   // Ć ć
        case 0x118: case 0x119: baza = 'e'; wariant = 1; break;   // Ę ę
        case 0x141: case 0x142: baza = 'l'; wariant = 1; break;   // Ł ł
        case 0x143: case 0x144: baza = 'n'; wariant = 1; break;   // Ń ń
        case 0xD3: case 0xF3: baza = 'o'; wariant = 1; break;     // Ó ó
        case 0x15A: case 0x15B: baza = 's'; wariant = 1; break;   // Ś ś
        case 0x179: case 0x17A: baza = 'z'; wariant = 1; break;   // Ź ź
        case 0x17B: case 0x17C: baza = 'z'; wariant = 2; break;   // Ż ż
        default:
            if (cp >= 0xC0 && cp <= 0xDE && cp != 0xD7) baza = cp + 32;   // Latin-1: wielkie -> male
            break;
    }
    if (bez_diakrytykow) return baza << 2;
    return (baza << 2) | wariant;
}

inline bool cyfra(uint32_t c) { return c >= '0' && c <= '9'; }

/*
 * Porownanie naturalne UTF-8: ciagi cyfr porownywane jako liczby (bez przepelnien), litery wg klucz_znaku().
 * Remis rozstrzyga porownanie bajtowe, wiec porzadek jest deterministyczny.
 */
inline int porownaj_naturalnie(const char* a, const char* b) {
    if (!a) a = "";
    if (!b) b = "";
    const auto* pa = reinterpret_cast<const uint8_t*>(a);
    const auto* pb = reinterpret_cast<const uint8_t*>(b);
    size_t ia = 0, ib = 0;
    const size_t na = dlugosc(a), nb = dlugosc(b);
    while (ia < na && ib < nb) {
        if (cyfra(pa[ia]) && cyfra(pb[ib])) {
            size_t za = ia, zb = ib;
            while (za < na && pa[za] == '0') ++za;
            while (zb < nb && pb[zb] == '0') ++zb;
            size_t ea = za, eb = zb;
            while (ea < na && cyfra(pa[ea])) ++ea;
            while (eb < nb && cyfra(pb[eb])) ++eb;
            if (ea - za != eb - zb) return ea - za < eb - zb ? -1 : 1;
            for (size_t i = 0; i < ea - za; ++i)
                if (pa[za + i] != pb[zb + i]) return pa[za + i] < pb[zb + i] ? -1 : 1;
            ia = ea; ib = eb;
            continue;
        }
        uint32_t ca = 0, cb = 0;
        size_t ka = sekwencja_utf8(pa + ia, na - ia, &ca), kb = sekwencja_utf8(pb + ib, nb - ib, &cb);
        if (!ka) { ca = pa[ia]; ka = 1; }
        if (!kb) { cb = pb[ib]; kb = 1; }
        const uint32_t xa = klucz_znaku(ca), xb = klucz_znaku(cb);
        if (xa != xb) return xa < xb ? -1 : 1;
        ia += ka; ib += kb;
    }
    if (ia < na) return 1;
    if (ib < nb) return -1;
    for (size_t i = 0; i < na && i < nb; ++i) if (pa[i] != pb[i]) return pa[i] < pb[i] ? -1 : 1;
    return na == nb ? 0 : (na < nb ? -1 : 1);
}

/*
 * Wyszukiwanie fragmentu: bez rozrozniania wielkosci liter i bez polskich znakow diakrytycznych.
 * Pusty wzorzec pasuje zawsze. Ograniczenie dlugosci: wzorzec <= 64 znaki, tekst <= 512 bajtow.
 */
inline bool zawiera(const char* tekst, const char* wzorzec) {
    if (!wzorzec || !wzorzec[0]) return true;
    if (!tekst) return false;
    uint32_t w[64]; size_t nw = 0;
    {
        const auto* p = reinterpret_cast<const uint8_t*>(wzorzec);
        const size_t n = dlugosc(wzorzec, 256);
        for (size_t i = 0; i < n && nw < 64;) {
            uint32_t cp = 0; size_t k = sekwencja_utf8(p + i, n - i, &cp);
            if (!k) { cp = p[i]; k = 1; }
            w[nw++] = klucz_znaku(cp, true);
            i += k;
        }
    }
    uint32_t t[512]; size_t nt = 0;
    {
        const auto* p = reinterpret_cast<const uint8_t*>(tekst);
        const size_t n = dlugosc(tekst, 512);
        for (size_t i = 0; i < n && nt < 512;) {
            uint32_t cp = 0; size_t k = sekwencja_utf8(p + i, n - i, &cp);
            if (!k) { cp = p[i]; k = 1; }
            t[nt++] = klucz_znaku(cp, true);
            i += k;
        }
    }
    if (nw > nt) return false;
    for (size_t s = 0; s + nw <= nt; ++s) {
        size_t j = 0;
        while (j < nw && t[s + j] == w[j]) ++j;
        if (j == nw) return true;
    }
    return false;
}

// Rozszerzenie pliku bez rozrozniania wielkosci liter ASCII (np. "Piosenka.MP3" ~ "mp3").
inline bool rozszerzenie(const char* nazwa, const char* ext) {
    const size_t n = dlugosc(nazwa), e = dlugosc(ext);
    if (n < e + 2 || nazwa[n - e - 1] != '.') return false;
    for (size_t i = 0; i < e; ++i) {
        char c = nazwa[n - e + i];
        if (c >= 'A' && c <= 'Z') c = char(c + 32);
        if (c != ext[i]) return false;
    }
    return true;
}

// Parsowanie liczby dziesietnej bez znaku z ograniczeniem (false przy przepelnieniu / braku cyfr).
inline bool parsuj_liczbe(const char* s, size_t n, uint64_t maks, uint64_t* out) {
    uint64_t v = 0; size_t i = 0;
    while (i < n && (s[i] == ' ')) ++i;
    const size_t start = i;
    for (; i < n && cyfra(uint8_t(s[i])); ++i) {
        const uint64_t d = uint64_t(s[i] - '0');
        if (v > maks / 10 || (v == maks / 10 && d > maks % 10)) return false;   // v * 10 + d > maks
        v = v * 10 + d;
    }
    if (i == start) return false;
    if (out) *out = v;
    return true;
}

} // namespace Grajek
