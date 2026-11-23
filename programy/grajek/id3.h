#pragma once
/*
 * Grajek - parser tagow ID3 (v1, v1.1, v2.2, v2.3, v2.4). Dane sa NIEZAUFANE.
 *
 * Zasady bezpieczenstwa:
 *  - do pamieci trafia co najwyzej MAKS_OKNO_TAGU bajtow tagu (bufor roboczy podany przez wywolujacego),
 *    reszta tagu jest pomijana bez czytania; kazda granica ramki jest sprawdzana wzgledem zadeklarowanego rozmiaru
 *    tagu, rozmiaru okna i rozmiaru pliku (arytmetyka 64-bitowa),
 *  - tekst ramki jest dekodowany z co najwyzej MAKS_TEKST_RAMKI bajtow i obcinany do pol Metadane,
 *  - ramki skompresowane / szyfrowane sa pomijane, rozmiary nie-syncsafe w v2.4 (blad iTunes) sa akceptowane tylko,
 *    gdy wskazuja na poprawny identyfikator nastepnej ramki,
 *  - okladka (APIC/PIC) NIE jest kopiowana: zapisywane jest tylko jej polozenie i rozmiar (<= MAKS_OKLADKA_BAJTY),
 *  - liczba ramek jest ograniczona (MAKS_RAMEK), wiec zlosliwy tag nie zapetli parsera.
 */
#include "metadane.h"
#include "zrodlo.h"

namespace Grajek::Id3 {

constexpr uint32_t MAKS_OKNO_TAGU = 256U * 1024U;
constexpr uint32_t MAKS_TEKST_RAMKI = 1024;
constexpr uint32_t MAKS_RAMEK = 512;
constexpr uint32_t MAKS_ROZMIAR_TAGU = 64U * 1024U * 1024U;

struct Naglowek {
    uint8_t wersja = 0;      // 2, 3 albo 4
    uint8_t flagi = 0;
    uint32_t rozmiar = 0;    // dane tagu po 10-bajtowym naglowku (bez stopki)
    uint32_t calkowity = 0;  // naglowek + dane + ewentualna stopka
};

inline bool syncsafe(const uint8_t* p, uint32_t* out) {
    if ((p[0] | p[1] | p[2] | p[3]) & 0x80) return false;
    *out = (uint32_t(p[0]) << 21) | (uint32_t(p[1]) << 14) | (uint32_t(p[2]) << 7) | uint32_t(p[3]);
    return true;
}

inline bool parsuj_naglowek(const uint8_t* h, size_t n, Naglowek* out) {
    if (n < 10 || h[0] != 'I' || h[1] != 'D' || h[2] != '3') return false;
    if (h[3] < 2 || h[3] > 4 || h[4] == 0xFF) return false;
    uint32_t r = 0;
    if (!syncsafe(h + 6, &r) || r > MAKS_ROZMIAR_TAGU) return false;
    out->wersja = h[3];
    out->flagi = h[5];
    out->rozmiar = r;
    out->calkowity = 10 + r + ((h[3] == 4 && (h[5] & 0x10)) ? 10 : 0);
    return true;
}

// Usuwa "unsynchronisation" (FF 00 -> FF) w miejscu; zwraca nowa dlugosc.
inline size_t usun_niesynchronizacje(uint8_t* d, size_t n) {
    size_t w = 0;
    for (size_t r = 0; r < n; ++r) {
        d[w++] = d[r];
        if (d[r] == 0xFF && r + 1 < n && d[r + 1] == 0x00) ++r;
    }
    return w;
}

inline void dekoduj_tekst(uint8_t kodowanie, const uint8_t* d, size_t n, char* out, size_t cap) {
    if (cap) out[0] = '\0';
    if (n > MAKS_TEKST_RAMKI) n = MAKS_TEKST_RAMKI;
    switch (kodowanie) {
        case 0: latin1_do_utf8(d, n, out, cap); break;
        case 1: utf16_do_utf8(d, n, false, true, out, cap); break;
        case 2: utf16_do_utf8(d, n, true, false, out, cap); break;
        case 3: { Tekst t(out, cap); t.dodaj_utf8(d, n); break; }
        default: break;
    }
    przytnij(out);
}

inline bool znak_id(uint8_t c) { return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'); }
inline bool id_poprawny(const uint8_t* p, size_t dl) {
    for (size_t i = 0; i < dl; ++i) if (!znak_id(p[i])) return false;
    return true;
}
inline bool id_rowny(const uint8_t* p, const char* id, size_t dl) {
    for (size_t i = 0; i < dl; ++i) if (p[i] != uint8_t(id[i])) return false;
    return true;
}

// "3/12" -> "3", "2026-11-13T14:02" -> "2026": pierwszy ciag cyfr o dlugosci <= maks.
inline void pierwsze_cyfry(const char* s, char* out, size_t cap, size_t maks) {
    if (cap) out[0] = '\0';
    size_t i = 0;
    while (s[i] && !cyfra(uint8_t(s[i]))) { if (s[i] != ' ') return; ++i; }
    size_t k = 0;
    while (s[i] && cyfra(uint8_t(s[i])) && k < maks && k + 1 < cap) out[k++] = s[i++];
    out[k] = '\0';
}

enum class RodzajRamki : uint8_t { Inna, Tytul, Wykonawca, WykonawcaAlbumu, Album, Numer, Rok, RokOryginalny, Okladka };

inline RodzajRamki rodzaj(const uint8_t* id, uint8_t wersja) {
    if (wersja == 2) {
        if (id_rowny(id, "TT2", 3)) return RodzajRamki::Tytul;
        if (id_rowny(id, "TP1", 3)) return RodzajRamki::Wykonawca;
        if (id_rowny(id, "TP2", 3)) return RodzajRamki::WykonawcaAlbumu;
        if (id_rowny(id, "TAL", 3)) return RodzajRamki::Album;
        if (id_rowny(id, "TRK", 3)) return RodzajRamki::Numer;
        if (id_rowny(id, "TYE", 3)) return RodzajRamki::Rok;
        if (id_rowny(id, "TOR", 3)) return RodzajRamki::RokOryginalny;
        if (id_rowny(id, "PIC", 3)) return RodzajRamki::Okladka;
        return RodzajRamki::Inna;
    }
    if (id_rowny(id, "TIT2", 4)) return RodzajRamki::Tytul;
    if (id_rowny(id, "TPE1", 4)) return RodzajRamki::Wykonawca;
    if (id_rowny(id, "TPE2", 4)) return RodzajRamki::WykonawcaAlbumu;
    if (id_rowny(id, "TALB", 4)) return RodzajRamki::Album;
    if (id_rowny(id, "TRCK", 4)) return RodzajRamki::Numer;
    if (id_rowny(id, "TYER", 4) || id_rowny(id, "TDRC", 4)) return RodzajRamki::Rok;
    if (id_rowny(id, "TORY", 4) || id_rowny(id, "TDOR", 4)) return RodzajRamki::RokOryginalny;
    if (id_rowny(id, "APIC", 4)) return RodzajRamki::Okladka;
    return RodzajRamki::Inna;
}

inline TypOkladki typ_z_mime(const uint8_t* m, size_t n) {
    char t[24]; size_t k = 0;
    for (size_t i = 0; i < n && k + 1 < sizeof(t); ++i) { char c = char(m[i]); if (c >= 'A' && c <= 'Z') c = char(c + 32); t[k++] = c; }
    t[k] = '\0';
    if (rowne(t, "image/jpeg") || rowne(t, "image/jpg") || rowne(t, "jpg") || rowne(t, "jpeg")) return TypOkladki::JPEG;
    if (rowne(t, "image/png") || rowne(t, "png")) return TypOkladki::PNG;
    return TypOkladki::Brak;
}

/*
 * Analiza tresci ramki APIC/PIC (body = poczatek danych ramki w pamieci, dostepne = ile bajtow jest w oknie,
 * calosc = pelny rozmiar danych ramki). Zwraca przesuniecie poczatku obrazu wzgledem body albo -1.
 */
inline int64_t analizuj_apic(const uint8_t* body, size_t dostepne, uint32_t calosc, uint8_t wersja,
                             TypOkladki* typ, uint8_t* typ_obrazu) {
    if (dostepne > calosc) dostepne = calosc;
    if (dostepne < 4) return -1;
    const uint8_t enc = body[0];
    if (enc > 3) return -1;
    size_t p = 1;
    if (wersja == 2) {
        if (dostepne < 5) return -1;
        *typ = typ_z_mime(body + 1, 3);
        p = 4;
    } else {
        size_t e = p;
        while (e < dostepne && e - p < 64 && body[e]) ++e;
        if (e >= dostepne || body[e]) return -1;
        *typ = typ_z_mime(body + p, e - p);
        p = e + 1;
    }
    if (p >= dostepne) return -1;
    *typ_obrazu = body[p++];
    // opis: zakonczony NUL (UTF-16: dwa bajty zerowe na granicy jednostki)
    if (enc == 1 || enc == 2) {
        while (p + 1 < dostepne && (body[p] || body[p + 1])) p += 2;
        if (p + 1 >= dostepne) return -1;
        p += 2;
    } else {
        while (p < dostepne && body[p]) ++p;
        if (p >= dostepne) return -1;
        p += 1;
    }
    if (p >= calosc) return -1;
    return int64_t(p);
}

/*
 * Parsuje ramki z okna tagu (dane PO 10-bajtowym naglowku). okno_dl moze byc mniejsze niz h.rozmiar, gdy tag jest
 * wiekszy od okna. offset_danych_w_pliku: polozenie okno[0] w pliku (zwykle 10) - wazne tylko, gdy okno nie
 * bylo przetworzone usunieciem niesynchronizacji (zmienia przesuniecia); wtedy offset_danych_w_pliku = UINT64_MAX.
 */
inline void parsuj_ramki(const uint8_t* okno, size_t okno_dl, const Naglowek& h, uint64_t offset_danych_w_pliku,
                         Metadane& m) {
    size_t pos = 0;
    const size_t deklarowane = h.rozmiar;
    if (okno_dl > deklarowane) okno_dl = deklarowane;
    // naglowek rozszerzony
    if (h.flagi & 0x40) {
        if (okno_dl < 6) return;
        uint32_t ext = 0;
        if (h.wersja == 3) ext = be32(okno) + 4;
        else if (h.wersja == 4) { if (!syncsafe(okno, &ext)) return; }
        else return;                          // v2.2: bit 0x40 = kompresja calego tagu - nieobslugiwana
        if (ext < 6 || ext >= okno_dl) return;
        pos = ext;
    }
    const size_t dl_id = h.wersja == 2 ? 3 : 4;
    const size_t dl_nag = h.wersja == 2 ? 6 : 10;
    char wykonawca_albumu[MAKS_POLE_TEKSTU] = {};
    char rok_oryg[8] = {};
    uint8_t tmp[MAKS_TEKST_RAMKI];
    for (uint32_t licznik = 0; licznik < MAKS_RAMEK; ++licznik) {
        if (pos + dl_nag > okno_dl) break;
        const uint8_t* f = okno + pos;
        if (f[0] == 0) break;                                     // dopelnienie
        if (!id_poprawny(f, dl_id)) break;
        uint32_t rozm = 0;
        if (h.wersja == 2) rozm = be24(f + 3);
        else if (h.wersja == 3) rozm = be32(f + 4);
        else {
            uint32_t ss = 0;
            const bool ok_ss = syncsafe(f + 4, &ss);
            const uint32_t surowy = be32(f + 4);
            rozm = ok_ss ? ss : surowy;
            // Zapisy iTunes: v2.4 z rozmiarem nie-syncsafe. Wybierz interpretacje wskazujaca na poprawna ramke.
            if (ok_ss && ss != surowy) {
                const size_t a = pos + dl_nag + ss, b = pos + dl_nag + size_t(surowy);
                const bool a_ok = a + dl_id <= okno_dl && (okno[a] == 0 || id_poprawny(okno + a, dl_id));
                const bool b_ok = b + dl_id <= okno_dl && id_poprawny(okno + b, dl_id);
                if (!a_ok && b_ok) rozm = surowy;
            }
        }
        if (uint64_t(pos) + dl_nag + rozm > deklarowane) break;  // ramka wychodzi poza tag: uszkodzony tag
        const uint8_t f1 = h.wersja >= 3 ? f[9] : 0;
        bool pomin = rozm == 0;
        size_t body_off = pos + dl_nag;
        uint32_t body_dl = rozm;
        bool ramka_unsync = (h.wersja == 4) && (f1 & 0x02);
        if (h.wersja == 3) {
            if (f1 & 0xC0) pomin = true;                         // kompresja / szyfrowanie
            if ((f1 & 0x20) && !pomin) { if (body_dl < 1) pomin = true; else { ++body_off; --body_dl; } }
        } else if (h.wersja == 4) {
            if (f1 & 0x0C) pomin = true;
            if ((f1 & 0x40) && !pomin) { if (body_dl < 1) pomin = true; else { ++body_off; --body_dl; } }
            if ((f1 & 0x01) && !pomin) { if (body_dl < 4) pomin = true; else { body_off += 4; body_dl -= 4; } }
        }
        if (h.wersja == 4 && (h.flagi & 0x80)) ramka_unsync = true;   // v2.4: flaga tagu = wszystkie ramki
        const RodzajRamki r = rodzaj(f, h.wersja);
        if (!pomin && r != RodzajRamki::Inna && body_off < okno_dl) {
            const size_t dostepne = okno_dl - body_off < body_dl ? okno_dl - body_off : body_dl;
            if (r == RodzajRamki::Okladka) {
                TypOkladki typ = TypOkladki::Brak; uint8_t typ_obr = 0xFF;
                const uint8_t* body = okno + body_off;
                size_t dost = dostepne;
                if (ramka_unsync) {
                    dost = dostepne < sizeof(tmp) ? dostepne : sizeof(tmp);
                    kopiuj_bajty(tmp, body, dost);
                    dost = usun_niesynchronizacje(tmp, dost);
                    body = tmp;
                }
                const int64_t start = analizuj_apic(body, dost, body_dl, h.wersja, &typ, &typ_obr);
                const bool lepsza = m.okladka == TypOkladki::Brak || (typ_obr == 3 && m.typ_obrazu_apic != 3);
                if (start > 0 && typ != TypOkladki::Brak && lepsza && offset_danych_w_pliku != UINT64_MAX) {
                    const uint64_t dane_dl = uint64_t(body_dl) - uint64_t(start);
                    if (ramka_unsync) {
                        // przesuniecia po usunieciu niesynchronizacji: okladka.h wczytuje cala ramke
                        if (body_dl <= MAKS_OKLADKA_BAJTY + 4096) {
                            m.okladka = typ; m.typ_obrazu_apic = typ_obr; m.okladka_niesynchronizowana = true;
                            m.okladka_offset_ramki = offset_danych_w_pliku + body_off;
                            m.okladka_rozmiar_ramki = body_dl;
                            m.okladka_offset = uint64_t(start);
                            m.okladka_rozmiar = 0;          // znany dopiero po usunieciu niesynchronizacji
                        }
                    } else if (dane_dl > 0 && dane_dl <= MAKS_OKLADKA_BAJTY) {
                        m.okladka = typ; m.typ_obrazu_apic = typ_obr; m.okladka_niesynchronizowana = false;
                        m.okladka_offset = offset_danych_w_pliku + body_off + uint64_t(start);
                        m.okladka_rozmiar = uint32_t(dane_dl);
                    }
                }
            } else {
                size_t n = dostepne < sizeof(tmp) ? dostepne : sizeof(tmp);
                kopiuj_bajty(tmp, okno + body_off, n);
                if (ramka_unsync) n = usun_niesynchronizacje(tmp, n);
                if (n >= 1) {
                    char tekst[MAKS_POLE_TEKSTU];
                    dekoduj_tekst(tmp[0], tmp + 1, n - 1, tekst, sizeof(tekst));
                    if (tekst[0]) {
                        switch (r) {
                            case RodzajRamki::Tytul: if (!m.tytul[0]) kopiuj_tekst(m.tytul, sizeof(m.tytul), tekst); break;
                            case RodzajRamki::Wykonawca: if (!m.wykonawca[0]) kopiuj_tekst(m.wykonawca, sizeof(m.wykonawca), tekst); break;
                            case RodzajRamki::WykonawcaAlbumu: if (!wykonawca_albumu[0]) kopiuj_tekst(wykonawca_albumu, sizeof(wykonawca_albumu), tekst); break;
                            case RodzajRamki::Album: if (!m.album[0]) kopiuj_tekst(m.album, sizeof(m.album), tekst); break;
                            case RodzajRamki::Numer: if (!m.numer[0]) pierwsze_cyfry(tekst, m.numer, sizeof(m.numer), 4); break;
                            case RodzajRamki::Rok: if (!m.rok[0]) pierwsze_cyfry(tekst, m.rok, sizeof(m.rok), 4); break;
                            case RodzajRamki::RokOryginalny: if (!rok_oryg[0]) pierwsze_cyfry(tekst, rok_oryg, sizeof(rok_oryg), 4); break;
                            default: break;
                        }
                    }
                }
            }
        }
        pos += dl_nag + rozm;
    }
    if (!m.wykonawca[0] && wykonawca_albumu[0]) kopiuj_tekst(m.wykonawca, sizeof(m.wykonawca), wykonawca_albumu);
    if (!m.rok[0] && rok_oryg[0]) kopiuj_tekst(m.rok, sizeof(m.rok), rok_oryg);
}

/*
 * Czyta tag ID3v2 spod przesuniecia `gdzie` (zwykle 0). bufor: roboczy, >= MAKS_OKNO_TAGU nie jest wymagane -
 * wczytywane jest min(rozmiar tagu, cap). Zwraca calkowity rozmiar tagu (do pominiecia) albo 0, gdy brak tagu.
 */
inline uint32_t czytaj_id3v2(Czytnik& c, uint64_t gdzie, Metadane& m, uint8_t* bufor, uint32_t cap) {
    uint8_t nag[10];
    if (!c.czytaj_w(gdzie, nag, 10)) return 0;
    Naglowek h;
    if (!parsuj_naglowek(nag, 10, &h)) return 0;
    if (gdzie + uint64_t(h.calkowity) > c.rozmiar()) {
        // tag dluzszy niz plik: parsuj to, co jest, ale nie pomijaj danych audio poza plikiem
        h.rozmiar = uint32_t(c.rozmiar() - gdzie - 10 < h.rozmiar ? c.rozmiar() - gdzie - 10 : h.rozmiar);
        h.calkowity = uint32_t(c.rozmiar() - gdzie);
    }
    if (bufor && cap >= 64 && h.rozmiar > 0) {
        uint32_t n = h.rozmiar < cap ? h.rozmiar : cap;
        if (c.czytaj_w(gdzie + 10, bufor, n)) {
            uint64_t offset = gdzie + 10;
            if (h.wersja <= 3 && (h.flagi & 0x80)) {
                n = uint32_t(usun_niesynchronizacje(bufor, n));
                offset = UINT64_MAX;            // przesuniecia w pliku nieznane: okladka pominieta
            }
            parsuj_ramki(bufor, n, h, offset, m);
        }
    }
    return h.calkowity;
}

// ID3v1/v1.1 z ostatnich 128 bajtow. Uzupelnia tylko puste pola. Zwraca true, gdy tag istnieje.
inline bool czytaj_id3v1(Czytnik& c, Metadane& m) {
    if (c.rozmiar() < 128 + 4) return false;
    uint8_t t[128];
    if (!c.czytaj_w(c.rozmiar() - 128, t, 128)) return false;
    if (t[0] != 'T' || t[1] != 'A' || t[2] != 'G') return false;
    auto pole = [](const uint8_t* p, size_t n, char* out, size_t cap) {
        uint8_t k[31]; size_t d = 0;
        for (; d < n && p[d]; ++d) k[d] = p[d];
        k[d] = 0;
        latin1_do_utf8(k, d, out, cap);
        przytnij(out);
    };
    char tmp[MAKS_POLE_TEKSTU];
    if (!m.tytul[0]) { pole(t + 3, 30, tmp, sizeof(tmp)); kopiuj_tekst(m.tytul, sizeof(m.tytul), tmp); }
    if (!m.wykonawca[0]) { pole(t + 33, 30, tmp, sizeof(tmp)); kopiuj_tekst(m.wykonawca, sizeof(m.wykonawca), tmp); }
    if (!m.album[0]) { pole(t + 63, 30, tmp, sizeof(tmp)); kopiuj_tekst(m.album, sizeof(m.album), tmp); }
    if (!m.rok[0]) {
        char r[5]; size_t k = 0;
        for (int i = 0; i < 4; ++i) if (cyfra(t[93 + i])) r[k++] = char(t[93 + i]);
        r[k] = '\0';
        if (k == 4) kopiuj_tekst(m.rok, sizeof(m.rok), r);
    }
    if (!m.numer[0] && t[125] == 0 && t[126] != 0) { Tekst n(m.numer, sizeof(m.numer)); n.liczba(t[126]); }
    return true;
}

} // namespace Grajek::Id3
