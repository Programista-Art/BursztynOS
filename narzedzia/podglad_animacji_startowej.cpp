// Podglad klatek ekranu powitalnego: czysty host, ten sam produkcyjny renderer.
// Zapisuje PPM (P6) w kluczowych chwilach animacji dla typowych rozdzielczosci
// i wypisuje geometrie ukladu. Bez emulatora i bez prawdziwego czekania.
//   g++ -std=c++17 -O2 -Ijadro/gui -I. narzedzia/podglad_animacji_startowej.cpp -o podglad
//   ./podglad KATALOG
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "ekran_powitalny.h"

static unsigned checks = 0;
#undef assert
#define assert(c) do { ++checks; if(!(c)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c); std::abort(); } } while(0)

int main(int argc, char** argv) {
    using namespace Powitanie;
    const char* katalog = argc > 1 ? argv[1] : ".";
    struct Tryb { int w, h; unsigned dpi; const char* nazwa; };
    const Tryb tryby[] = { {1024,768,100,"1024x768"}, {1280,720,100,"1280x720"},
                           {1366,768,100,"1366x768"}, {1920,1080,100,"1920x1080"},
                           {1920,1080,125,"1920x1080-125"}, {800,600,100,"800x600"} };
    // Probki: logo, wejscie tytulu, tytul, separator, podtytul, drugi podtytul, komplet.
    const uint64_t czasy[] = {0, 300000, 1200000, 1600000, 2200000, 2600000,
                              3250000, 3600000, 4250000, 4600000};
    for (const auto& t : tryby) {
        std::vector<uint32_t> obraz(size_t(t.w) * t.h, 0x00FF00FF); // magenta = niepomalowane
        for (uint64_t czas : czasy) {
            Sekwencja s;
            s.rozpocznij(true, 0);
            s.rozpocznij_etapy(0); // kompozytor aktywny od pierwszej klatki
            s.aktualizuj(czas);
            rysuj(obraz.data(), t.w, t.h, t.w, t.dpi, s, true);
            char nazwa[256];
            snprintf(nazwa, sizeof(nazwa), "%s/powitanie-%s-%04llums.ppm", katalog,
                     t.nazwa, (unsigned long long)(czas / 1000));
            FILE* f = fopen(nazwa, "wb");
            assert(f != nullptr);
            fprintf(f, "P6\n%d %d\n255\n", t.w, t.h);
            for (int y = 0; y < t.h; ++y)
                for (int x = 0; x < t.w; ++x) {
                    const uint32_t c = obraz[size_t(y) * t.w + x];
                    const unsigned char rgb[3] = { (unsigned char)((c >> 16) & 255),
                                                   (unsigned char)((c >> 8) & 255),
                                                   (unsigned char)(c & 255) };
                    fwrite(rgb, 1, 3, f);
                }
            fclose(f);
            assert(obraz[0] == TLO);
        }
        const auto g = geometria(t.w, t.h, t.dpi);
        const auto& m = g.m;
        printf("%s@%u%%: skala %d, logo %d,%d %dx%d | tytul %d,%d %dx%d (baza %d) | separator %d,%d %dx%d | "
               "l1 %d,%d %dx%d | l2 %d,%d %dx%d | blok %d (y=%d..%d)\n",
               t.nazwa, t.dpi, m.skala, g.logo_rdzen.x, g.logo_rdzen.y, g.logo_rdzen.w, g.logo_rdzen.h,
               g.tytul.r.x, g.tytul.r.y, g.tytul.r.w, g.tytul.r.h, g.tytul.baza,
               g.separator.x, g.separator.y, g.separator.w, g.separator.h,
               g.linia1.r.x, g.linia1.r.y, g.linia1.r.w, g.linia1.r.h,
               g.linia2.r.x, g.linia2.r.y, g.linia2.r.w, g.linia2.r.h,
               m.blok_h, g.logo_rdzen.y, g.linia2.r.y + g.linia2.r.h);
        assert(g.logo.y >= 0);
        assert(g.linia2.r.y + g.linia2.r.h <= t.h);
        assert(g.dirty.x >= 0 && g.dirty.x + g.dirty.w <= t.w);
    }
    printf("Podglad klatek: %u/%u PASS\n", checks, checks);
    return 0;
}
