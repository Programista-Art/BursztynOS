// Host test centralnego kursora zmiany rozmiaru (jadro/gui/kursor_rozciagania.h).
#include <stdio.h>
#include <stdint.h>
#include <initializer_list>
#include "jadro/gui/kursor_rozciagania.h"
#include "jadro/gui/okno_geometria.h"
#include "jadro/bws/bws_nowoczesne.h"

static int bledy = 0, sprawdzen = 0;
#define CHECK(c) do { ++sprawdzen; if (!(c)) { ++bledy; printf("BLAD %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

int main() {
    using namespace OknoGeometria;
    // Typy niebedace zmiana rozmiaru nigdy nie dostaja zasobu.
    for (uint32_t t : {uint32_t(GUI_CURSOR_ARROW), uint32_t(GUI_CURSOR_HAND), uint32_t(GUI_CURSOR_TEXT),
                       uint32_t(GUI_CURSOR_WAIT), uint32_t(GUI_CURSOR_CROSSHAIR), 7u, 100u, 0xFFFFFFFFu})
        CHECK(kursor_rozciagania_opis(t) == nullptr);
    // Wartosci enum jadra i geometrii musza byc zgodne (header geometrii powiela stale).
    CHECK(KURSOR_NS == GUI_CURSOR_RESIZE_NS && KURSOR_EW == GUI_CURSOR_RESIZE_EW);
    CHECK(KURSOR_NWSE == GUI_CURSOR_RESIZE_NWSE && KURSOR_NESW == GUI_CURSOR_RESIZE_NESW);
#if KURSOR_ROZCIAGANIA_DOSTEPNY
    printf("zasob kursora: DOSTEPNY\n");
    // Wszystkie osiem stref chwytania -> typ -> zasob w granicach 64x64, hotspot w obrazie i na tresci.
    const uint8_t strefy[8] = {KRAWEDZ_LEWA, KRAWEDZ_PRAWA, KRAWEDZ_GORNA, KRAWEDZ_DOLNA,
        uint8_t(KRAWEDZ_LEWA | KRAWEDZ_GORNA), uint8_t(KRAWEDZ_PRAWA | KRAWEDZ_GORNA),
        uint8_t(KRAWEDZ_LEWA | KRAWEDZ_DOLNA), uint8_t(KRAWEDZ_PRAWA | KRAWEDZ_DOLNA)};
    for (uint8_t m : strefy) {
        const uint32_t typ = kursor_dla_krawedzi(m);
        const OpisKursoraRozciagania* o = kursor_rozciagania_opis(typ);
        CHECK(o != nullptr);
        if (!o) continue;
        CHECK(o->szer > 0 && o->wys > 0 && o->szer <= GUI_CURSOR_CUSTOM_MAX_W && o->wys <= GUI_CURSOR_CUSTOM_MAX_H);
        CHECK(o->hotspot_x < o->szer && o->hotspot_y < o->wys);
        // Hotspot = srodek ikony (wskaznik zgodny z krawedzia): srodek ma byc w obrebie tresci (bbox).
        int minx = 999, miny = 999, maxx = -1, maxy = -1, opaque = 0;
        for (int y = 0; y < o->wys; ++y)
            for (int x = 0; x < o->szer; ++x) {
                uint32_t c;
                if (kursor_rozciagania_piksel(o, x, y, &c)) {
                    ++opaque; if (x < minx) minx = x; if (x > maxx) maxx = x; if (y < miny) miny = y; if (y > maxy) maxy = y;
                    CHECK((o->piksele[y * o->szer + x] >> 24) == 0xFF);   // alfa binarna
                }
            }
        CHECK(opaque > 20);
        CHECK(o->hotspot_x >= minx && o->hotspot_x <= maxx && o->hotspot_y >= miny && o->hotspot_y <= maxy);
        // Srodek tresci nie odbiega od hotspotu o wiecej niz 2 px (brak przesuniecia wzgledem krawedzi).
        CHECK(((minx + maxx + 1) / 2 - o->hotspot_x) * ((minx + maxx + 1) / 2 - o->hotspot_x) <= 4);
        CHECK(((miny + maxy + 1) / 2 - o->hotspot_y) * ((miny + maxy + 1) / 2 - o->hotspot_y) <= 4);
    }
    // EW jest pozioma (szerszy niz wysoki), NS pionowa; NS to obrot EW (te same wymiary zamienione miejscami).
    const auto* ew = kursor_rozciagania_opis(GUI_CURSOR_RESIZE_EW);
    const auto* ns = kursor_rozciagania_opis(GUI_CURSOR_RESIZE_NS);
    CHECK(ew->szer > ew->wys && ns->wys > ns->szer && ew->szer == ns->wys && ew->wys == ns->szer);
    // Proporcje zrodla 128x66 zachowane (bez znieksztalcenia): |szer/wys - 128/66| < 0.2 po przycieciu do bbox +-1 px.
    int bx0 = 999, bx1 = -1, by0 = 999, by1 = -1; uint32_t c;
    for (int y = 0; y < ew->wys; ++y) for (int x = 0; x < ew->szer; ++x)
        if (kursor_rozciagania_piksel(ew, x, y, &c)) { if (x < bx0) bx0 = x; if (x > bx1) bx1 = x; if (y < by0) by0 = y; if (y > by1) by1 = y; }
    const double wspol = double(bx1 - bx0 + 1) / double(by1 - by0 + 1);
    CHECK(wspol > 1.7 && wspol < 2.1);
    // NS jest dokladnym obrotem EW o 90 stopni: pikselowa zgodnosc ksztaltu (liczba pikseli taka sama).
    int n_ew = 0, n_ns = 0;
    for (int y = 0; y < ew->wys; ++y) for (int x = 0; x < ew->szer; ++x) n_ew += kursor_rozciagania_piksel(ew, x, y, &c);
    for (int y = 0; y < ns->wys; ++y) for (int x = 0; x < ns->szer; ++x) n_ns += kursor_rozciagania_piksel(ns, x, y, &c);
    CHECK(n_ew > 0 && (n_ew - n_ns) * (n_ew - n_ns) <= (n_ew / 10) * (n_ew / 10));
#else
    printf("zasob kursora: BRAK (uzywany ksztalt wbudowany)\n");
    for (uint32_t t = 3; t <= 6; ++t) CHECK(kursor_rozciagania_opis(t) == nullptr);
#endif
    // Poza obrazem i wskaznik zerowy: bezpiecznie false.
    uint32_t cc = 0;
    CHECK(!kursor_rozciagania_piksel(nullptr, 0, 0, &cc));
#if KURSOR_ROZCIAGANIA_DOSTEPNY
    const auto* o = kursor_rozciagania_opis(GUI_CURSOR_RESIZE_EW);
    CHECK(!kursor_rozciagania_piksel(o, -1, 0, &cc) && !kursor_rozciagania_piksel(o, 0, -1, &cc));
    CHECK(!kursor_rozciagania_piksel(o, o->szer, 0, &cc) && !kursor_rozciagania_piksel(o, 0, o->wys, &cc));
#endif
    printf("%s: %d sprawdzen, %d bledow\n", bledy ? "FAIL" : "PASS", sprawdzen, bledy);
    return bledy != 0;
}
