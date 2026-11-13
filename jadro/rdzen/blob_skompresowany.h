#pragma once
/*
 * Bursztyn OS - skompresowane bloby aplikacji wbudowanych w obraz jadra.
 *
 * Format (narzedzia/kompresuj_blob.py):
 *   "BZB1" | u32 LE rozmiar_surowy | u32 LE crc32(surowe) | strumien zlib
 *
 * Dane sa czescia zaufanego obrazu jadra (ta sama integralnosc co dotad).
 * Dekompresja ma stale, znane z naglowka ograniczenie wyjscia, sprawdza
 * Adler-32 strumienia i CRC-32 calego wyniku (przyrostowo, patrz Strumien). Zaden format fontu ani dane
 * z zewnatrz nie trafiaja tu - to tylko wlasne zasoby jadra.
 */
#include <stdint.h>
#include <stddef.h>

namespace BlobZ {
constexpr uint32_t MAGIC = 0x31425a42u; // "BZB1" little-endian
constexpr uint32_t MAKS_ROZMIAR = 4u * 1024u * 1024u;

inline uint32_t le32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
inline bool czy_skompresowany(const uint8_t* p, const uint8_t* k) {
    return p && k && k > p && uint64_t(k - p) >= 14 && le32(p) == MAGIC;
}
inline bool rozmiar_surowy(const uint8_t* p, const uint8_t* k, uint32_t* out) {
    if (!czy_skompresowany(p, k)) return false;
    uint32_t n = le32(p + 4);
    if (!n || n > MAKS_ROZMIAR) return false;
    *out = n;
    return true;
}

// Kanoniczny dekoder Huffmana z tablica szybkiego wyszukiwania (9 bitow).
struct Tablica {
    uint16_t szybka[512];      // (dlugosc << 12) | symbol, 0 = wolna sciezka
    uint16_t liczba[16];
    uint16_t symbole[320];
};
struct Stan {
    const uint8_t* wej; size_t n, poz;
    uint64_t bity; unsigned liczba_bitow;
    uint8_t* wyj; size_t poj, dl;
    bool blad;
    void uzupelnij() {
        while (liczba_bitow <= 56) {
            uint64_t b = poz < n ? wej[poz] : 0;
            if (poz < n) ++poz; else if (liczba_bitow == 0) { blad = true; return; } else return;
            bity |= b << liczba_bitow; liczba_bitow += 8;
        }
    }
    uint32_t pobierz(unsigned k) {
        if (!k) return 0;
        if (liczba_bitow < k) uzupelnij();
        if (liczba_bitow < k) { blad = true; return 0; }
        uint32_t v = uint32_t(bity & ((1ull << k) - 1));
        bity >>= k; liczba_bitow -= k;
        return v;
    }
};
inline int buduj(Tablica& t, const uint8_t* dl, unsigned n) {
    for (auto& c : t.liczba) c = 0;
    for (auto& s : t.szybka) s = 0;
    for (unsigned s = 0; s < n; ++s) ++t.liczba[dl[s]];
    if (t.liczba[0] == n) return 0;
    int zostalo = 1;
    for (unsigned d = 1; d < 16; ++d) { zostalo <<= 1; zostalo -= t.liczba[d]; if (zostalo < 0) return -1; }
    uint16_t off[16]{};
    for (unsigned d = 1; d < 15; ++d) off[d + 1] = uint16_t(off[d] + t.liczba[d]);
    for (unsigned s = 0; s < n; ++s) if (dl[s]) t.symbole[off[dl[s]]++] = uint16_t(s);
    // Szybka tablica: kody <= 9 bitow, odwrocone bitowo (DEFLATE: LSB first).
    unsigned kod = 0, idx = 0;
    for (unsigned d = 1; d <= 9; ++d) {
        for (unsigned i = 0; i < t.liczba[d]; ++i, ++kod, ++idx) {
            unsigned r = 0;
            for (unsigned b = 0; b < d; ++b) r |= ((kod >> b) & 1u) << (d - 1 - b);
            for (unsigned w = r; w < 512; w += 1u << d) t.szybka[w] = uint16_t((d << 12) | t.symbole[idx]);
        }
        kod <<= 1;
    }
    return zostalo;
}
inline int dekoduj(Stan& s, const Tablica& t) {
    if (s.liczba_bitow < 15) s.uzupelnij();
    uint16_t e = t.szybka[s.bity & 511];
    if (e && unsigned(e >> 12) <= s.liczba_bitow) {
        unsigned d = e >> 12; s.bity >>= d; s.liczba_bitow -= d; return e & 0xfff;
    }
    int kod = 0, pierwszy = 0, indeks = 0;
    for (unsigned d = 1; d < 16; ++d) {
        kod |= int(s.pobierz(1));
        if (s.blad) return -1;
        int c = t.liczba[d];
        if (kod - c < pierwszy) return t.symbole[indeks + (kod - pierwszy)];
        indeks += c; pierwszy += c; pierwszy <<= 1; kod <<= 1;
    }
    return -1;
}
enum class Kody { KONIEC_BLOKU, LIMIT, BLAD };
// Dekoduje symbole bloku, az do konca bloku albo do osiagniecia `cel` bajtow
// wyjscia (miekki limit: biezace dopasowanie <= 258 B jest zawsze konczone;
// pojemnosc bufora jest sprawdzana niezaleznie).
inline Kody kody(Stan& s, const Tablica& lit, const Tablica& odl, size_t cel) {
    static const uint16_t lb[29] = {3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
    static const uint8_t le[29] = {0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
    static const uint16_t ob[30] = {1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
    static const uint8_t oe[30] = {0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};
    for (;;) {
        if (s.dl >= cel) return Kody::LIMIT;
        int sym = dekoduj(s, lit);
        if (sym < 0) return Kody::BLAD;
        if (sym < 256) { if (s.dl >= s.poj) return Kody::BLAD; s.wyj[s.dl++] = uint8_t(sym); continue; }
        if (sym == 256) return Kody::KONIEC_BLOKU;
        sym -= 257;
        if (sym >= 29) return Kody::BLAD;
        size_t dlg = lb[sym] + s.pobierz(le[sym]);
        int d = dekoduj(s, odl);
        if (d < 0 || d >= 30 || s.blad) return Kody::BLAD;
        size_t o = ob[d] + s.pobierz(oe[d]);
        if (s.blad || o > s.dl || dlg > s.poj - s.dl) return Kody::BLAD;
        for (size_t i = 0; i < dlg; ++i, ++s.dl) s.wyj[s.dl] = s.wyj[s.dl - o];
    }
}
// Strumien wznawialny: instalator zapisuje plik porcjami po 4 KiB i rozpakowuje
// tylko tyle, ile zaraz zapisze, wiec zaden krok nie blokuje CPU na czas
// dekompresji calego pliku. Adler-32 i CRC-32 licza sie przyrostowo; wynik
// jest zaakceptowany dopiero przez zakoncz().
struct Strumien {
    Stan s{};
    Tablica lit{}, odl{};
    bool ostatni = false, w_bloku = false, koniec = false;
    uint32_t adler_a = 1, adler_b = 0, crc = ~0u;
    size_t sprawdzone = 0;
    const uint8_t* naglowek = nullptr;
};
inline uint32_t crc32_dalej(uint32_t c, const uint8_t* p, size_t n) {
    static const uint32_t t[16] = {0x00000000u,0x1db71064u,0x3b6e20c8u,0x26d930acu,0x76dc4190u,0x6b6b51f4u,0x4db26158u,0x5005713cu,
                                   0xedb88320u,0xf00f9344u,0xd6d6a3e8u,0xcb61b38cu,0x9b64c2b0u,0x86d3d2d4u,0xa00ae278u,0xbdbdf21cu};
    for (size_t i = 0; i < n; ++i) { c ^= p[i]; c = (c >> 4) ^ t[c & 15]; c = (c >> 4) ^ t[c & 15]; }
    return c;
}
// Otwiera strumien do bufora `wyj` o pojemnosci dokladnie rozmiar_surowy.
inline bool otworz(Strumien& z, const uint8_t* p, const uint8_t* k, uint8_t* wyj, uint32_t poj) {
    z = Strumien{};
    uint32_t n = 0;
    if (!rozmiar_surowy(p, k, &n) || n != poj || !wyj) return false;
    const uint8_t* d = p + 12;
    size_t dn = size_t(k - d);
    if (dn < 6 || (d[0] & 15) != 8 || (d[0] >> 4) > 7 || ((unsigned(d[0]) << 8) | d[1]) % 31 || (d[1] & 0x20)) return false;
    z.s = Stan{d + 2, dn - 2, 0, 0, 0, wyj, poj, 0, false};
    z.naglowek = p;
    return true;
}
// Rozpakowuje co najmniej do `cel` bajtow wyjscia (albo do konca strumienia;
// cel >= rozmiar oznacza "do konca, lacznie ze znacznikiem konca bloku").
inline bool dalej(Strumien& z, size_t cel) {
    Stan& s = z.s;
    bool do_konca = cel >= s.poj;
    while (!z.koniec && (do_konca || s.dl < cel)) {
        if (!z.w_bloku) {
            if (z.ostatni) { z.koniec = true; break; }
            z.ostatni = s.pobierz(1);
            unsigned typ = s.pobierz(2);
            if (s.blad) return false;
            if (typ == 0) {
                // Wyrownanie do bajtu: zwroc nieuzyte pelne bajty bufora bitow.
                unsigned reszta = s.liczba_bitow & 7; s.bity >>= reszta; s.liczba_bitow -= reszta;
                while (s.liczba_bitow >= 8) { --s.poz; s.liczba_bitow -= 8; }
                s.bity = 0; s.liczba_bitow = 0;
                if (s.poz + 4 > s.n) return false;
                unsigned len = s.wej[s.poz] | (unsigned(s.wej[s.poz + 1]) << 8), nlen = s.wej[s.poz + 2] | (unsigned(s.wej[s.poz + 3]) << 8);
                s.poz += 4;
                if (len != (~nlen & 0xffffu) || len > s.n - s.poz || len > s.poj - s.dl) return false;
                for (unsigned i = 0; i < len; ++i) s.wyj[s.dl++] = s.wej[s.poz++];
                continue;
            }
            if (typ == 1) {
                uint8_t d[288 + 30]; unsigned i = 0;
                for (; i < 144; ++i) d[i] = 8;
                for (; i < 256; ++i) d[i] = 9;
                for (; i < 280; ++i) d[i] = 7;
                for (; i < 288; ++i) d[i] = 8;
                for (i = 0; i < 30; ++i) d[288 + i] = 5;
                if (buduj(z.lit, d, 288) < 0 || buduj(z.odl, d + 288, 30) < 0) return false;
            } else if (typ == 2) {
                unsigned nl = s.pobierz(5) + 257, nd = s.pobierz(5) + 1, nk = s.pobierz(4) + 4;
                if (s.blad || nl > 286 || nd > 30) return false;
                static const uint8_t kolej[19] = {16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
                uint8_t d[320]{};
                for (unsigned i = 0; i < nk; ++i) d[kolej[i]] = uint8_t(s.pobierz(3));
                if (s.blad || buduj(z.lit, d, 19) != 0) return false;
                unsigned i = 0;
                while (i < nl + nd) {
                    int sym = dekoduj(s, z.lit);
                    if (sym < 0) return false;
                    if (sym < 16) { d[i++] = uint8_t(sym); continue; }
                    unsigned w = 0, r;
                    if (sym == 16) { if (!i) return false; w = d[i - 1]; r = 3 + s.pobierz(2); }
                    else if (sym == 17) r = 3 + s.pobierz(3);
                    else r = 11 + s.pobierz(7);
                    if (s.blad || i + r > nl + nd) return false;
                    while (r--) d[i++] = uint8_t(w);
                }
                if (!d[256]) return false;
                int e = buduj(z.lit, d, nl);
                if (e < 0 || (e > 0 && nl - z.lit.liczba[0] != 1)) return false;
                e = buduj(z.odl, d + nl, nd);
                if (e < 0 || (e > 0 && nd - z.odl.liczba[0] != 1)) return false;
            } else return false;
            z.w_bloku = true;
        }
        Kody w = kody(s, z.lit, z.odl, do_konca ? s.poj + 1 : cel);
        if (w == Kody::BLAD) return false;
        if (w == Kody::KONIEC_BLOKU) z.w_bloku = false;
    }
    if (s.blad) return false;
    // Sumy kontrolne nowo zdekodowanych bajtow.
    for (size_t i = z.sprawdzone; i < s.dl; ++i) {
        z.adler_a = (z.adler_a + s.wyj[i]) % 65521u; z.adler_b = (z.adler_b + z.adler_a) % 65521u;
    }
    z.crc = crc32_dalej(z.crc, s.wyj + z.sprawdzone, s.dl - z.sprawdzone);
    z.sprawdzone = s.dl;
    return true;
}
// Konczy strumien: caly wynik, Adler-32 strumienia i CRC-32 z naglowka.
inline bool zakoncz(Strumien& z) {
    if (!dalej(z, z.s.poj) || !z.koniec || z.s.dl != z.s.poj) return false;
    Stan& s = z.s;
    unsigned reszta = s.liczba_bitow & 7; s.bity >>= reszta; s.liczba_bitow -= reszta;
    while (s.liczba_bitow >= 8) { --s.poz; s.liczba_bitow -= 8; }
    s.bity = 0; s.liczba_bitow = 0;
    if (s.poz + 4 > s.n) return false;
    uint32_t oczek = (uint32_t(s.wej[s.poz]) << 24) | (uint32_t(s.wej[s.poz + 1]) << 16) | (uint32_t(s.wej[s.poz + 2]) << 8) | s.wej[s.poz + 3];
    if (((z.adler_b << 16) | z.adler_a) != oczek) return false;
    return ~z.crc == le32(z.naglowek + 8);
}
// Rozpakowuje caly blob do `wyj` (dokladnie rozmiar_surowy bajtow).
// `z` jest duze (~3,4 KiB tablic) - wywolujacy podaje je, aby nie lezalo
// na stosie jadra. Zwraca false przy jakimkolwiek bledzie.
inline bool rozpakuj(Strumien& z, const uint8_t* p, const uint8_t* k, uint8_t* wyj, uint32_t poj) {
    return otworz(z, p, k, wyj, poj) && zakoncz(z);
}
} // namespace BlobZ
