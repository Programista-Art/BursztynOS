#pragma once
/*
 * Grajek - rozpoznanie strumienia MPEG-1/2/2.5 Layer III bez dekodowania (skan biblioteki, walidacja pliku).
 *
 * Rozszerzenie .mp3 NIE wystarcza: plik musi zawierac co najmniej dwie kolejne, spojne ramki Layer III
 * (ten sam MPEG i czestotliwosc, druga ramka dokladnie tam, gdzie wskazuje dlugosc pierwszej) w pierwszych
 * MAKS_SKAN_SYNC bajtach za tagami ID3v2. Czas trwania: naglowek Xing/Info albo VBRI (pliki VBR), w przeciwnym
 * razie oszacowanie CBR z rozmiaru danych i przeplywnosci pierwszej ramki.
 */
#include "id3.h"

namespace Grajek::Mp3 {

constexpr uint32_t MAKS_SKAN_SYNC = 128U * 1024U;

struct Ramka {
    uint8_t wersja = 0;          // 1 = MPEG-1, 2 = MPEG-2, 25 = MPEG-2.5
    uint32_t czestotliwosc = 0;
    uint32_t przeplywnosc = 0;   // kbit/s
    uint16_t kanaly = 0;
    uint32_t dlugosc = 0;        // bajty ramki
    uint32_t probki = 0;         // probki na ramke (1152 / 576)
};

inline bool parsuj_ramke(const uint8_t* h, Ramka* r) {
    if (h[0] != 0xFF || (h[1] & 0xE0) != 0xE0) return false;
    const uint8_t ver = (h[1] >> 3) & 3;     // 0 = 2.5, 1 = zarezerwowane, 2 = MPEG-2, 3 = MPEG-1
    const uint8_t layer = (h[1] >> 1) & 3;   // 1 = Layer III
    if (ver == 1 || layer != 1) return false;
    const uint8_t br = h[2] >> 4, sr = (h[2] >> 2) & 3, pad = (h[2] >> 1) & 1;
    if (br == 0 || br == 15 || sr == 3) return false;   // "free format" nieobslugiwany
    if ((h[3] & 3) == 2) return false;                  // zarezerwowany emphasis
    static constexpr uint16_t BR1[15] = {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320};
    static constexpr uint16_t BR2[15] = {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160};
    static constexpr uint32_t SR[3] = {44100, 48000, 32000};
    r->wersja = ver == 3 ? 1 : ver == 2 ? 2 : 25;
    r->czestotliwosc = SR[sr] >> (ver == 3 ? 0 : ver == 2 ? 1 : 2);
    r->przeplywnosc = ver == 3 ? BR1[br] : BR2[br];
    r->kanaly = ((h[3] >> 6) == 3) ? 1 : 2;
    r->probki = ver == 3 ? 1152 : 576;
    r->dlugosc = (ver == 3 ? 144000U : 72000U) * r->przeplywnosc / r->czestotliwosc + pad;
    return r->dlugosc >= 24;
}

struct Info {
    uint32_t czestotliwosc = 0;
    uint16_t kanaly = 0;
    uint32_t przeplywnosc = 0;   // kbit/s (pierwsza ramka albo srednia VBR)
    bool vbr = false;
    uint64_t czas_ms = 0;        // 0 = nieznany
    uint64_t ramki_pcm = 0;      // z Xing/VBRI (0 = nieznane)
    uint64_t pierwsza_ramka = 0; // przesuniecie pierwszej ramki audio w pliku
    uint32_t rozmiar_id3 = 0;
};

enum class Blad : uint8_t { OK, Odczyt, BrakRamek };

inline const char* opis(Blad b) {
    switch (b) {
        case Blad::OK: return "OK";
        case Blad::Odczyt: return "Błąd odczytu pliku.";
        case Blad::BrakRamek: return "Plik nie zawiera poprawnych ramek MPEG Layer III.";
    }
    return "Nieznany błąd MP3.";
}

// Przesuniecie informacji bocznych w ramce (dla Xing/Info).
inline uint32_t przesuniecie_xing(const Ramka& r) {
    if (r.wersja == 1) return 4 + (r.kanaly == 1 ? 17 : 32);
    return 4 + (r.kanaly == 1 ? 9 : 17);
}

/*
 * Analiza pliku. bufor_id3/cap: bufor roboczy dla tagu ID3v2 (metadane), moze byc nullptr. meta: wynik (opcjonalny).
 */
inline Blad analizuj(Czytnik& c, Info& out, Metadane* meta, uint8_t* bufor_id3, uint32_t cap_id3) {
    out = Info{};
    uint64_t pos = 0;
    Metadane pomocnicze;
    Metadane& m = meta ? *meta : pomocnicze;
    // kolejne tagi ID3v2 (niektore programy dopisuja kilka)
    for (int i = 0; i < 4; ++i) {
        const uint32_t t = Id3::czytaj_id3v2(c, pos, m, i == 0 ? bufor_id3 : nullptr, i == 0 ? cap_id3 : 0);
        if (!t) break;
        if (i == 0) out.rozmiar_id3 = t;
        pos += t;
    }
    if (meta) (void)Id3::czytaj_id3v1(c, m);
    // szukanie synchronizacji: dwie kolejne spojne ramki
    uint8_t okno[4096 + 4];
    uint64_t skan = pos;
    const uint64_t limit = pos + MAKS_SKAN_SYNC < c.rozmiar() ? pos + MAKS_SKAN_SYNC : c.rozmiar();
    Ramka r{};
    bool znaleziono = false;
    while (skan + 4 <= limit && !znaleziono) {
        uint64_t n = limit - skan;
        if (n > sizeof(okno)) n = sizeof(okno);
        if (n < 4 || !c.czytaj_w(skan, okno, uint32_t(n))) return Blad::Odczyt;
        for (uint32_t i = 0; i + 4 <= n; ++i) {
            if (okno[i] != 0xFF) continue;
            Ramka a{};
            if (!parsuj_ramke(okno + i, &a)) continue;
            const uint64_t nast = skan + i + a.dlugosc;
            uint8_t h2[4];
            Ramka b{};
            if (nast + 4 > c.rozmiar() || !c.czytaj_w(nast, h2, 4) || !parsuj_ramke(h2, &b)) continue;
            if (b.wersja != a.wersja || b.czestotliwosc != a.czestotliwosc) continue;
            r = a;
            out.pierwsza_ramka = skan + i;
            znaleziono = true;
            break;
        }
        if (!znaleziono) skan += n - 3;
    }
    if (!znaleziono) return Blad::BrakRamek;
    out.czestotliwosc = r.czestotliwosc;
    out.kanaly = r.kanaly;
    out.przeplywnosc = r.przeplywnosc;
    // Xing / Info / VBRI w pierwszej ramce
    uint8_t pr[192] = {};
    const uint32_t ile = r.dlugosc < sizeof(pr) ? r.dlugosc : uint32_t(sizeof(pr));
    uint64_t ramek_mpeg = 0;
    uint64_t bajtow = 0;
    if (c.czytaj_w(out.pierwsza_ramka, pr, ile)) {
        const uint32_t x = przesuniecie_xing(r);
        if (x + 16 <= ile && ((pr[x] == 'X' && pr[x + 1] == 'i' && pr[x + 2] == 'n' && pr[x + 3] == 'g') ||
                              (pr[x] == 'I' && pr[x + 1] == 'n' && pr[x + 2] == 'f' && pr[x + 3] == 'o'))) {
            const uint32_t flagi = be32(pr + x + 4);
            uint32_t p = x + 8;
            if (flagi & 1) { ramek_mpeg = be32(pr + p); p += 4; }
            if ((flagi & 2) && p + 4 <= ile) { bajtow = be32(pr + p); }
            out.vbr = pr[x] == 'X';
        } else if (36 + 18 <= ile && pr[36] == 'V' && pr[37] == 'B' && pr[38] == 'R' && pr[39] == 'I') {
            bajtow = be32(pr + 36 + 10);
            ramek_mpeg = be32(pr + 36 + 14);
            out.vbr = true;
        }
    }
    if (ramek_mpeg > 0 && ramek_mpeg < (1ULL << 32)) {
        out.ramki_pcm = ramek_mpeg * r.probki;
        out.czas_ms = out.ramki_pcm * 1000ULL / r.czestotliwosc;
        if (bajtow && out.czas_ms) out.przeplywnosc = uint32_t(bajtow * 8ULL / out.czas_ms);
    } else {
        uint64_t dane = c.rozmiar() - out.pierwsza_ramka;
        uint8_t t[3];
        if (c.rozmiar() >= 128 && c.czytaj_w(c.rozmiar() - 128, t, 3) && t[0] == 'T' && t[1] == 'A' && t[2] == 'G' && dane > 128)
            dane -= 128;
        if (r.przeplywnosc) out.czas_ms = dane * 8ULL / r.przeplywnosc;
    }
    return Blad::OK;
}

} // namespace Grajek::Mp3
