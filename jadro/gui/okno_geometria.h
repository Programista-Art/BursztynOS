#pragma once
/*
 * Geometria okien: zmiana rozmiaru krawedziami/rogami i ruch poza ekran.
 *
 * Czysta logika bez zaleznosci od BWS (testowalna na hoscie), uzywana przez
 * BursztynUI::Okno oraz przez aplikacje z wlasna petla zdarzen (Eksplorator).
 * Wspolrzedne i rozmiary sa int32; wszystkie obliczenia posrednie w int64 z
 * jawnym przycinaniem, wiec skrajne wartosci wskaznika nie daja przepelnienia
 * ani ujemnych rozmiarow. Pozycja logiczna okna moze byc ujemna lub wykraczac
 * poza ekran - kompozytor przycina do framebuffera (int64 w clippingu).
 */
#include <stdint.h>

namespace OknoGeometria {

struct Prost {
    int32_t x, y, w, h;
    constexpr bool operator==(const Prost& o) const {
        return x == o.x && y == o.y && w == o.w && h == o.h;
    }
};

enum Krawedz : uint8_t {
    KRAWEDZ_BRAK = 0,
    KRAWEDZ_LEWA = 1,
    KRAWEDZ_PRAWA = 2,
    KRAWEDZ_GORNA = 4,
    KRAWEDZ_DOLNA = 8,
};

// Wartosci GUI_CURSOR_* z bws_nowoczesne.h (3=NS, 4=EW, 5=NWSE, 6=NESW);
// 0 = strzalka. Powielone, by header nie ciagnal ABI jadra.
constexpr uint32_t KURSOR_STRZALKA = 0, KURSOR_NS = 3, KURSOR_EW = 4, KURSOR_NWSE = 5, KURSOR_NESW = 6;

constexpr int64_t klamp(int64_t v, int64_t lo, int64_t hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

constexpr int32_t do_i32(int64_t v) {
    return static_cast<int32_t>(klamp(v, INT32_MIN, INT32_MAX));
}

// Strefa chwytania: pas `grubosc` px wzdluz krawedzi (wewnatrz okna), a w rogach
// kwadrat `naroznik` px. Punkt (lx,ly) jest lokalny wzgledem okna. Zwraca maske.
constexpr uint8_t krawedz_w_punkcie(int32_t lx, int32_t ly, int32_t w, int32_t h,
                                    int32_t grubosc, int32_t naroznik) {
    if (w <= 0 || h <= 0 || lx < 0 || ly < 0 || lx >= w || ly >= h) return KRAWEDZ_BRAK;
    if (grubosc < 1) grubosc = 1;
    if (naroznik < grubosc) naroznik = grubosc;
    // Przy bardzo malym oknie rogi nie moga zajac calej powierzchni.
    if (static_cast<int64_t>(naroznik) * 2 > w) naroznik = w / 2 < grubosc ? grubosc : w / 2;
    if (static_cast<int64_t>(naroznik) * 2 > h) naroznik = h / 2 < grubosc ? grubosc : h / 2;
    const bool l = lx < grubosc, r = lx >= w - grubosc, t = ly < grubosc, b = ly >= h - grubosc;
    uint8_t m = KRAWEDZ_BRAK;
    // Naroznik: blisko krawedzi w jednej osi i w obrebie `naroznik` w drugiej.
    if (l || lx < naroznik) { if (t || ly < naroznik) { if (l || t) m = KRAWEDZ_LEWA | KRAWEDZ_GORNA; } }
    if (!m && (r || lx >= w - naroznik)) { if (t || ly < naroznik) { if (r || t) m = KRAWEDZ_PRAWA | KRAWEDZ_GORNA; } }
    if (!m && (l || lx < naroznik)) { if (b || ly >= h - naroznik) { if (l || b) m = KRAWEDZ_LEWA | KRAWEDZ_DOLNA; } }
    if (!m && (r || lx >= w - naroznik)) { if (b || ly >= h - naroznik) { if (r || b) m = KRAWEDZ_PRAWA | KRAWEDZ_DOLNA; } }
    if (m) return m;
    if (l) return KRAWEDZ_LEWA;
    if (r) return KRAWEDZ_PRAWA;
    if (t) return KRAWEDZ_GORNA;
    if (b) return KRAWEDZ_DOLNA;
    return KRAWEDZ_BRAK;
}

constexpr uint32_t kursor_dla_krawedzi(uint8_t m) {
    const bool lr = (m & (KRAWEDZ_LEWA | KRAWEDZ_PRAWA)) != 0, tb = (m & (KRAWEDZ_GORNA | KRAWEDZ_DOLNA)) != 0;
    if (lr && tb) {
        const bool nwse = ((m & KRAWEDZ_LEWA) && (m & KRAWEDZ_GORNA)) || ((m & KRAWEDZ_PRAWA) && (m & KRAWEDZ_DOLNA));
        return nwse ? KURSOR_NWSE : KURSOR_NESW;
    }
    if (lr) return KURSOR_EW;
    if (tb) return KURSOR_NS;
    return KURSOR_STRZALKA;
}

struct Ograniczenia {
    int32_t min_w, min_h, max_w, max_h;
};

// Nowa geometria po przeciagnieciu o (dx,dy) od `start`. Przeciwlegle krawedzie
// pozostaja nieruchome; rozmiar jest przycinany do [min,max], a krawedz
// zakotwiczona obliczana z krawedzi przeciwleglej, dzieki czemu przy
// osiagnieciu minimum okno nie "ucieka" za kursorem.
constexpr Prost zmien_rozmiar(Prost start, uint8_t krawedzie, int32_t dx, int32_t dy, Ograniczenia lim) {
    if (lim.min_w < 1) lim.min_w = 1;
    if (lim.min_h < 1) lim.min_h = 1;
    if (lim.max_w < lim.min_w) lim.max_w = lim.min_w;
    if (lim.max_h < lim.min_h) lim.max_h = lim.min_h;
    const int64_t prawa = static_cast<int64_t>(start.x) + start.w;
    const int64_t dolna = static_cast<int64_t>(start.y) + start.h;
    int64_t x = start.x, y = start.y, w = start.w, h = start.h;
    if (krawedzie & KRAWEDZ_PRAWA) w = klamp(static_cast<int64_t>(start.w) + dx, lim.min_w, lim.max_w);
    if (krawedzie & KRAWEDZ_LEWA) {
        w = klamp(static_cast<int64_t>(start.w) - dx, lim.min_w, lim.max_w);
        x = prawa - w;
    }
    if (krawedzie & KRAWEDZ_DOLNA) h = klamp(static_cast<int64_t>(start.h) + dy, lim.min_h, lim.max_h);
    if (krawedzie & KRAWEDZ_GORNA) {
        h = klamp(static_cast<int64_t>(start.h) - dy, lim.min_h, lim.max_h);
        y = dolna - h;
    }
    return {do_i32(x), do_i32(y), do_i32(w), do_i32(h)};
}

// Obszar roboczy (bez paska zadan). Ruch swobodny, ale okno musi pozostac
// odzyskiwalne: co najmniej `uchwyt_x` px paska tytulu widoczne poziomo w
// obszarze roboczym, a gorna krawedz nie wyzej niz `nad_gora` px nad nim i
// pasek tytulu (wysokosc `belka`) nie ponizej dolnej krawedzi obszaru.
struct LimityRuchu {
    int32_t min_x, max_x, min_y, max_y;
};

constexpr LimityRuchu limity_ruchu(Prost okno, Prost praca, int32_t uchwyt_x, int32_t belka, int32_t nad_gora) {
    if (uchwyt_x < 1) uchwyt_x = 1;
    if (belka < 1) belka = 1;
    if (static_cast<int64_t>(uchwyt_x) > okno.w) uchwyt_x = okno.w < 1 ? 1 : okno.w;
    const int64_t pr = static_cast<int64_t>(praca.x) + praca.w;
    const int64_t pd = static_cast<int64_t>(praca.y) + praca.h;
    return {
        do_i32(static_cast<int64_t>(praca.x) - okno.w + uchwyt_x),   // lewa krawedz tak, ze widac `uchwyt_x` px
        do_i32(pr - uchwyt_x),                                         // prawa strona analogicznie
        do_i32(static_cast<int64_t>(praca.y) - nad_gora),
        do_i32(pd - belka),
    };
}

constexpr Prost przytnij_ruch(Prost okno, Prost praca, int32_t uchwyt_x, int32_t belka, int32_t nad_gora) {
    const LimityRuchu l = limity_ruchu(okno, praca, uchwyt_x, belka, nad_gora);
    return {do_i32(klamp(okno.x, l.min_x, l.max_x)), do_i32(klamp(okno.y, l.min_y, l.max_y)), okno.w, okno.h};
}

// Czy pasek tytulu jest uchwytny (do automatycznego odzyskania okna po zmianie
// rozdzielczosci albo wyjsciu obszaru roboczego poza okno).
constexpr bool pasek_uchwytny(Prost okno, Prost praca, int32_t uchwyt_x, int32_t belka, int32_t nad_gora) {
    const LimityRuchu l = limity_ruchu(okno, praca, uchwyt_x, belka, nad_gora);
    return okno.x >= l.min_x && okno.x <= l.max_x && okno.y >= l.min_y && okno.y <= l.max_y;
}

} // namespace OknoGeometria
