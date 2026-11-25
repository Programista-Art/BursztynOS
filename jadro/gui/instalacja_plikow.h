#pragma once
// Instalator Bursztyn OS: kontrola plikow systemowych (etap SYSTEMFILES/VERIFY) z ustrukturyzowana diagnostyka.
//
// Wspolny kod jadra (jadro/rdzen/kernel.cpp) i testu hosta (testy/test_instalacja_host.cpp): zapis wylicza
// CRC-32 calego pliku i kazdego segmentu 64 KiB, weryfikacja czyta plik z PSF porcjami 4 KiB i przy pierwszej
// niezgodnosci zapisuje Diagnoza (etap, plik, oczekiwano/odczytano, przesuniecie, LBA, stan zapisu trwalego).
// Bez alokacji i bez biblioteki standardowej; wszystkie bufory tekstowe sa ograniczone.
#include <stdint.h>
#include <stddef.h>

namespace InstalacjaPlikow {

constexpr uint32_t MAKS_SCIEZKA = 128;          // = PlikKontrolny::path (z NUL)
constexpr uint32_t SEGMENT = 64u * 1024u;       // granulacja lokalizacji niezgodnosci
constexpr uint32_t MAKS_SEGMENTOW = 2048;       // 128 MiB danych kontrolnych; powyzej tylko CRC calego pliku
constexpr uint32_t PORCJA = 4u * 1024u;         // jeden blok BSP2 na krok weryfikacji

enum class Etap : uint8_t { Brak = 0, Plan, Katalogi, Zapis, Weryfikacja };
enum class Przyczyna : uint8_t {
    Brak = 0, Limit, Katalog, Utworzenie, Rozpakowanie, Zapis, Flush,   // zapis
    BrakPliku, Rozmiar, Odczyt, Crc,                                    // weryfikacja
    ZrodloNietrwale, ZrodloZmienione,                                   // zrodlo wpisu planu (blad wewnetrzny)
};

// --- zrodlo wpisu planu krokowego ---
// Plan SYSTEMFILES zapamietuje tylko WSKAZNIK do bajtow pliku; zapis nastepuje w kolejnych krokach instalatora
// (osobne wywolania), wiec zrodlo musi przetrwac caly etap: obraz jadra albo zarezerwowany modul Multiboot2.
// Lokalna tablica funkcji planujacej (stos) po powrocie jest nadpisywana przez kolejne wywolania - wtedy na
// dysk trafiaja przypadkowe bajty, a CRC wyliczone z tej samej (juz zmienionej) pamieci rozni sie od nosnika.
constexpr uint32_t MAKS_MIGAWKI_ZRODLA = 64u * 1024u;   // CRC zrodla w chwili planowania (male pliki surowe)
struct Zrodlo { uint32_t crc; bool znane; };
// Czy [p, p+n) nachodzi na [dol, gora) (n == 0 traktowane jak 1 bajt; przepelnienie = nachodzi).
inline bool zakres_nachodzi(const void* p, uint64_t n, uintptr_t dol, uintptr_t gora) {
    if (!p || dol >= gora) return false;
    const uintptr_t a = uintptr_t(p);
    const uint64_t dl = n ? n : 1;
    if (dl > uint64_t(UINTPTR_MAX - a)) return true;
    return a < gora && a + uintptr_t(dl) > dol;
}

struct Diagnoza {
    Etap etap;
    Przyczyna przyczyna;
    char sciezka[MAKS_SCIEZKA];
    uint64_t oczekiwano, odczytano;    // rozmiar w bajtach albo CRC-32 (zalezy od przyczyny)
    uint64_t offset, dlugosc;          // zakres w pliku (porcja albo segment)
    uint64_t lba;                      // bezwzgledny LBA dysku pierwszego bloku zakresu (gdy lba_znane)
    uint32_t indeks_pliku, liczba_plikow;
    bool lba_znane, zapis_trwaly;      // zapis_trwaly: stan PSF w chwili bledu
    bool flush_ok, flush_znany;        // wynik ostatniego FLUSH PSF
    // Niezgodnosc CRC segmentu: drugi odczyt tego samego segmentu z nosnika (po porzuceniu czystego cache).
    bool ponowny_znany, ponowny_odczyt_ok;
    uint32_t crc_ponowny;
    uint64_t czas_odczytu_us;          // czas ostatniego odczytu przed bledem (0 = brak zegara)
    uint64_t maks_czas_odczytu_us;     // najdluzszy odczyt 4 KiB w tej weryfikacji
};

struct Kontrolny {
    char sciezka[MAKS_SCIEZKA];
    uint32_t rozmiar, crc;
    uint32_t pierwszy_segment, segmentow;   // segmentow == 0: tylko CRC calego pliku
};

// Dostep do PSF (produkcja: funkcje psf.h; test: te same funkcje albo atrapy z bledami).
struct Odczyt {
    bool (*rozmiar)(const char* sciezka, uint64_t* rozmiar);                  // false = brak pliku
    bool (*czytaj)(const char* sciezka, uint64_t offset, void* bufor, uint32_t n, uint32_t* odczytano);
    bool (*lba)(const char* sciezka, uint64_t indeks_bloku, uint64_t* lba);    // opcjonalne
    bool (*zapis_trwaly)();                                                    // opcjonalne
    bool (*porzuc_cache)();     // opcjonalne: porzuca tylko CZYSTE bloki cache (odczyt z nosnika)
    uint64_t (*czas_us)();      // opcjonalne: zegar monotoniczny
    bool (*flush_ok)();         // opcjonalne: wynik ostatniego FLUSH
};

inline uint32_t crc32_dalej(uint32_t c, const uint8_t* d, uint32_t n) {
    for (uint32_t i = 0; i < n; ++i) {
        c ^= d[i];
        for (unsigned b = 0; b < 8; ++b) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return c;
}

inline void kopiuj(char* d, size_t cap, const char* s) {
    if (!d || !cap) return;
    size_t i = 0;
    for (; s && s[i] && i + 1 < cap; ++i) d[i] = s[i];
    d[i] = 0;
}

inline void ustaw(Diagnoza* d, Etap e, Przyczyna p, const char* sciezka, uint64_t oczekiwano = 0,
                  uint64_t odczytano = 0, uint64_t offset = 0, uint64_t dlugosc = 0) {
    if (!d) return;
    *d = Diagnoza{};
    d->etap = e; d->przyczyna = p;
    kopiuj(d->sciezka, sizeof(d->sciezka), sciezka);
    d->oczekiwano = oczekiwano; d->odczytano = odczytano; d->offset = offset; d->dlugosc = dlugosc;
}

// --- strona zapisu: CRC calego pliku i segmentow 64 KiB w kolejnosci zapisu ---
struct Rejestr {
    uint32_t crc = ~0u, crc_segmentu = ~0u, w_segmencie = 0;
    uint32_t pierwszy = 0, segmentow = 0;
    bool przepelnienie = false;
};
inline void rejestr_start(Rejestr& r, uint32_t liczba_segmentow) { r = Rejestr{}; r.pierwszy = liczba_segmentow; }
inline void rejestr_dodaj(Rejestr& r, const uint8_t* d, uint32_t n, uint32_t* segmenty, uint32_t* liczba_segmentow) {
    r.crc = crc32_dalej(r.crc, d, n);
    while (n) {
        uint32_t k = SEGMENT - r.w_segmencie;
        if (k > n) k = n;
        r.crc_segmentu = crc32_dalej(r.crc_segmentu, d, k);
        r.w_segmencie += k; d += k; n -= k;
        if (r.w_segmencie == SEGMENT) {
            if (*liczba_segmentow < MAKS_SEGMENTOW && segmenty) segmenty[(*liczba_segmentow)++] = ~r.crc_segmentu, ++r.segmentow;
            else r.przepelnienie = true;
            r.crc_segmentu = ~0u; r.w_segmencie = 0;
        }
    }
}
inline void rejestr_zakoncz(Rejestr& r, Kontrolny& k, const char* sciezka, uint32_t rozmiar,
                            uint32_t* segmenty, uint32_t* liczba_segmentow) {
    if (r.w_segmencie) {
        if (*liczba_segmentow < MAKS_SEGMENTOW && segmenty) segmenty[(*liczba_segmentow)++] = ~r.crc_segmentu, ++r.segmentow;
        else r.przepelnienie = true;
    }
    kopiuj(k.sciezka, sizeof(k.sciezka), sciezka);
    k.rozmiar = rozmiar; k.crc = ~r.crc;
    k.pierwszy_segment = r.pierwszy;
    k.segmentow = r.przepelnienie ? 0 : r.segmentow;
}

// Migawka CRC zrodla w chwili planowania (tylko male pliki surowe; wieksze leza w obrazie jadra).
inline Zrodlo migawka_zrodla(const void* d, uint32_t n) {
    if (n > MAKS_MIGAWKI_ZRODLA || (n && !d)) return Zrodlo{0, false};
    return Zrodlo{~crc32_dalej(~0u, static_cast<const uint8_t*>(d), n), true};
}
// Czy bajty zapisane na dysk (CRC z rejestru) sa tymi samymi bajtami, ktore byly w zrodle przy planowaniu.
inline bool zrodlo_zgodne(const Zrodlo& z, const Kontrolny& k) { return !z.znane || z.crc == k.crc; }

// --- strona weryfikacji: jeden krok = co najwyzej jedna porcja 4 KiB ---
struct Weryfikacja {
    uint32_t indeks = 0;
    uint64_t offset = 0;
    uint32_t crc = ~0u, crc_segmentu = ~0u;
    uint64_t bajty = 0;
    uint64_t maks_czas_us = 0;
};

inline void uzupelnij_lba(const Odczyt& o, Diagnoza* d) {
    if (!d) return;
    if (o.zapis_trwaly) d->zapis_trwaly = o.zapis_trwaly();
    if (o.flush_ok) { d->flush_ok = o.flush_ok(); d->flush_znany = true; }
    uint64_t lba = 0;
    if (d->dlugosc && o.lba && o.lba(d->sciezka, d->offset / 4096u, &lba)) { d->lba = lba; d->lba_znane = true; }
}

inline bool weryfikuj_krok(Weryfikacja& w, const Kontrolny* pliki, uint32_t liczba, const uint32_t* segmenty,
                           const Odczyt& o, uint8_t* bufor, bool* gotowe, Diagnoza* d) {
    if (gotowe) *gotowe = false;
    if (w.indeks >= liczba) { if (gotowe) *gotowe = true; return true; }
    const Kontrolny& k = pliki[w.indeks];
    uint64_t czas_odczytu = 0;
    auto blad = [&](Przyczyna p, uint64_t ocz, uint64_t odcz, uint64_t off, uint64_t dl) {
        ustaw(d, Etap::Weryfikacja, p, k.sciezka, ocz, odcz, off, dl);
        if (d) {
            d->indeks_pliku = w.indeks; d->liczba_plikow = liczba;
            d->czas_odczytu_us = czas_odczytu; d->maks_czas_odczytu_us = w.maks_czas_us;
        }
        uzupelnij_lba(o, d);
        return false;
    };
    // Drugi odczyt segmentu [pocz, pocz+dl) wprost z nosnika: odroznia niestabilny odczyt (drugi = oczekiwany)
    // od trwale innych bajtow na dysku (drugi = pierwszy). Ograniczony do jednego segmentu 64 KiB.
    auto ponow = [&](uint64_t pocz, uint64_t dl) {
        if (!d) return;
        if (o.porzuc_cache && !o.porzuc_cache()) return;
        uint32_t c = ~0u;
        for (uint64_t off = 0; off < dl;) {
            const uint32_t n = uint32_t(dl - off < PORCJA ? dl - off : PORCJA);
            uint32_t got = 0;
            if (!o.czytaj(k.sciezka, pocz + off, bufor, n, &got) || got != n) { d->ponowny_znany = true; d->ponowny_odczyt_ok = false; return; }
            c = crc32_dalej(c, bufor, n);
            off += n;
        }
        d->ponowny_znany = true; d->ponowny_odczyt_ok = true; d->crc_ponowny = ~c;
    };
    if (w.offset == 0) {
        uint64_t r = 0;
        if (!o.rozmiar(k.sciezka, &r)) return blad(Przyczyna::BrakPliku, k.rozmiar, 0, 0, 0);
        if (r != k.rozmiar) return blad(Przyczyna::Rozmiar, k.rozmiar, r, 0, 0);
    }
    uint32_t n = k.rozmiar - uint32_t(w.offset);
    if (n > PORCJA) n = PORCJA;
    if (n) {
        uint32_t got = 0;
        const uint64_t t0 = o.czas_us ? o.czas_us() : 0;
        const bool ok = o.czytaj(k.sciezka, w.offset, bufor, n, &got);
        czas_odczytu = o.czas_us ? o.czas_us() - t0 : 0;
        if (czas_odczytu > w.maks_czas_us) w.maks_czas_us = czas_odczytu;
        if (!ok || got != n) return blad(Przyczyna::Odczyt, n, got, w.offset, n);
        w.crc = crc32_dalej(w.crc, bufor, n);
        w.crc_segmentu = crc32_dalej(w.crc_segmentu, bufor, n);
    }
    w.offset += n; w.bajty += n;
    const bool koniec_pliku = w.offset == k.rozmiar;
    if (k.segmentow && segmenty && (w.offset % SEGMENT == 0 || koniec_pliku) && n) {
        const uint64_t seg = (w.offset - 1) / SEGMENT;
        const uint32_t oczekiwany = segmenty[k.pierwszy_segment + seg];
        if (~w.crc_segmentu != oczekiwany) {
            const uint64_t pocz = seg * SEGMENT;
            blad(Przyczyna::Crc, oczekiwany, ~w.crc_segmentu, pocz, w.offset - pocz);
            ponow(pocz, w.offset - pocz);
            return false;
        }
        w.crc_segmentu = ~0u;
    }
    if (koniec_pliku) {
        if (~w.crc != k.crc) return blad(Przyczyna::Crc, k.crc, ~w.crc, 0, k.rozmiar);
        ++w.indeks; w.offset = 0; w.crc = ~0u; w.crc_segmentu = ~0u;
    }
    if (w.indeks == liczba && gotowe) *gotowe = true;
    return true;
}

// --- opisy ---
inline const char* token_etapu(Etap e) {
    switch (e) { case Etap::Plan: return "PLAN"; case Etap::Katalogi: return "MKDIR"; case Etap::Zapis: return "WRITE";
                 case Etap::Weryfikacja: return "VERIFY"; default: return "NONE"; }
}
inline const char* token_przyczyny(Przyczyna p) {
    switch (p) {
    case Przyczyna::Limit: return "limit"; case Przyczyna::Katalog: return "mkdir"; case Przyczyna::Utworzenie: return "create";
    case Przyczyna::Rozpakowanie: return "unpack"; case Przyczyna::Zapis: return "write"; case Przyczyna::Flush: return "flush";
    case Przyczyna::BrakPliku: return "missing"; case Przyczyna::Rozmiar: return "size-mismatch";
    case Przyczyna::Odczyt: return "read-error"; case Przyczyna::Crc: return "crc-mismatch";
    case Przyczyna::ZrodloNietrwale: return "transient-source"; case Przyczyna::ZrodloZmienione: return "source-changed";
    default: return "none";
    }
}
inline const char* opis_przyczyny(Przyczyna p) {
    switch (p) {
    case Przyczyna::Limit: return "przekroczony limit planu instalacji";
    case Przyczyna::Katalog: return "nie można utworzyć katalogu";
    case Przyczyna::Utworzenie: return "nie można utworzyć pliku";
    case Przyczyna::Rozpakowanie: return "uszkodzony wbudowany plik (rozpakowanie)";
    case Przyczyna::Zapis: return "błąd zapisu na dysk";
    case Przyczyna::Flush: return "błąd FLUSH dysku";
    case Przyczyna::BrakPliku: return "plik nie istnieje";
    case Przyczyna::Rozmiar: return "niezgodny rozmiar";
    case Przyczyna::Odczyt: return "błąd odczytu z dysku";
    case Przyczyna::Crc: return "niezgodna zawartość (CRC-32)";
    case Przyczyna::ZrodloNietrwale: return "dane źródłowe w pamięci tymczasowej (błąd wewnętrzny instalatora)";
    case Przyczyna::ZrodloZmienione: return "dane źródłowe zmieniły się podczas zapisu (błąd wewnętrzny instalatora)";
    default: return "nieznany błąd";
    }
}

struct Pisarz {
    char* out; size_t cap, p = 0;
    Pisarz(char* o, size_t c) : out(o), cap(c) { if (out && cap) out[0] = 0; }
    void t(const char* s) { while (s && *s && out && p + 1 < cap) out[p++] = *s++; if (out && cap) out[p] = 0; }
    void u(uint64_t v) { char b[21]; int n = 0; do { b[n++] = char('0' + v % 10); v /= 10; } while (v && n < 20); while (n && out && p + 1 < cap) out[p++] = b[--n]; if (out && cap) out[p] = 0; }
    void x(uint64_t v) { t("0x"); char b[17]; int n = 0; do { b[n++] = "0123456789abcdef"[v & 15]; v >>= 4; } while (v && n < 16); while (n && out && p + 1 < cap) out[p++] = b[--n]; if (out && cap) out[p] = 0; }
    // Sciezka UTF-8 obcinana tylko na granicy znaku (bez polowy znaku w GUI).
    void sciezka(const char* s, size_t maks) {
        size_t n = 0; while (s && s[n]) ++n;
        if (n <= maks) { t(s); return; }
        size_t start = n - maks;
        while (start < n && (uint8_t(s[start]) & 0xC0) == 0x80) ++start;
        t("..."); t(s + start);
    }
};

// Linia logu szeregowego (ASCII poza sciezka; ograniczona dlugosc).
inline void linia_logu(const Diagnoza& d, char* out, size_t cap) {
    Pisarz w(out, cap);
    w.t("[INSTALL DIAG] phase="); w.t(token_etapu(d.etap));
    w.t(" cause="); w.t(token_przyczyny(d.przyczyna));
    w.t(" object="); w.sciezka(d.sciezka, 100);
    if (d.liczba_plikow && d.indeks_pliku < d.liczba_plikow) { w.t(" file="); w.u(d.indeks_pliku + 1); w.t("/"); w.u(d.liczba_plikow); }
    const bool crc = d.przyczyna == Przyczyna::Crc || d.przyczyna == Przyczyna::ZrodloZmienione;
    if (d.przyczyna == Przyczyna::Rozmiar || d.przyczyna == Przyczyna::BrakPliku || crc || d.przyczyna == Przyczyna::Odczyt) {
        w.t(" expected="); crc ? w.x(d.oczekiwano) : w.u(d.oczekiwano);
        w.t(" actual="); crc ? w.x(d.odczytano) : w.u(d.odczytano);
    }
    if (d.dlugosc) {
        w.t(" offset="); w.u(d.offset); w.t(" length="); w.u(d.dlugosc);
        w.t(" blocks="); w.u(d.offset / 4096u); w.t("-"); w.u((d.offset + d.dlugosc - 1) / 4096u);
    }
    if (d.lba_znane) { w.t(" lba="); w.u(d.lba); }
    if (d.przyczyna == Przyczyna::Crc && d.ponowny_znany) {
        if (!d.ponowny_odczyt_ok) w.t(" reread=read-error");
        else {
            w.t(" reread="); w.x(d.crc_ponowny);
            w.t(d.crc_ponowny == d.oczekiwano ? "(matches-expected:unstable-read)"
                : d.crc_ponowny == d.odczytano ? "(same-as-first:medium-content)" : "(differs:unstable-read)");
        }
    }
    if (d.etap == Etap::Weryfikacja) { w.t(" read_us="); w.u(d.czas_odczytu_us); w.t(" max_read_us="); w.u(d.maks_czas_odczytu_us); }
    if (d.flush_znany) { w.t(" flush="); w.t(d.flush_ok ? "ok" : "failed"); }
    w.t(" persistent="); w.t(d.zapis_trwaly ? "1" : "0");
}

// Komunikat dla GUI instalatora (po polsku, bez zawartosci pliku): trzy wiersze rozdzielone '\n' -
// etap (z numerem pliku), sciezka, przyczyna z zakresem blokow. GUI zawija tekst (Etykieta::ustaw_zawijanie);
// pelny rekord (CRC, LBA, ponowny odczyt, czasy) jest tylko w logu szeregowym (linia_logu).
inline void opisz(const Diagnoza& d, char* out, size_t cap) {
    Pisarz w(out, cap);
    w.t(d.etap == Etap::Weryfikacja ? "Weryfikacja plików systemowych" : "Zapis plików systemowych");
    if (d.liczba_plikow && d.indeks_pliku < d.liczba_plikow) {
        w.t(" (plik "); w.u(d.indeks_pliku + 1); w.t(" z "); w.u(d.liczba_plikow); w.t(")");
    }
    w.t(":\n"); w.sciezka(d.sciezka, 100); w.t("\n");
    const size_t pierwsza = w.p;
    w.t(opis_przyczyny(d.przyczyna));
    if (out && pierwsza < w.p && out[pierwsza] >= 'a' && out[pierwsza] <= 'z') out[pierwsza] = char(out[pierwsza] - 'a' + 'A');
    if (d.przyczyna == Przyczyna::Rozmiar) { w.t(" ("); w.u(d.odczytano); w.t(" zamiast "); w.u(d.oczekiwano); w.t(" B)"); }
    else if (d.dlugosc && d.przyczyna != Przyczyna::BrakPliku) {
        const uint64_t a = d.offset / 4096u, b = (d.offset + d.dlugosc - 1) / 4096u;
        if (a == b) { w.t(", blok "); w.u(a); } else { w.t(", bloki "); w.u(a); w.t("–"); w.u(b); }
    }
    w.t(".");
}

}  // namespace InstalacjaPlikow
