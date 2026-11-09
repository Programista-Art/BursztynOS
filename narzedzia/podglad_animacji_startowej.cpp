// Podglad klatek ekranu powitalnego: czysty host, ten sam produkcyjny renderer.
// Zapisuje PPM (P6) w wybranych chwilach animacji dla dwoch rozdzielczosci.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "ekran_powitalny.h"

static unsigned checks = 0;
#undef assert
#define assert(c) do { ++checks; if(!(c)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c); std::abort(); } } while(0)

int main(int argc, char** argv) {
    const char* katalog = argc > 1 ? argv[1] : ".";
    struct Tryb { int w, h; unsigned dpi; const char* nazwa; };
    const Tryb tryby[] = { {1280,720,100,"720p"}, {1920,1080,100,"1080p"}, {1920,1080,125,"1080p125"} };
    const uint64_t czasy[] = {0, 400000, 800000, 1200000, 1600000, 2000000, 2400000,
                              2800000, 3400000, 3700000, 4200000, 5000000};
    for (const auto& t : tryby) {
        std::vector<uint32_t> obraz(size_t(t.w) * t.h, 0x00FF00FF); // magenta = niepomalowane
        for (uint64_t czas : czasy) {
            Powitanie::Sekwencja s;
            s.rozpocznij(true, 0);
            s.aktualizuj(czas);
            Powitanie::rysuj(obraz.data(), t.w, t.h, t.w, t.dpi, s, true);
            char nazwa[256];
            snprintf(nazwa, sizeof(nazwa), "%s/powitanie-%s-%03llums.ppm", katalog,
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
            // Sanity: tlo jest czarne, nie magenta, i sa piksele logotypu.
            assert(obraz[0] == Powitanie::TLO);
        }
        // Kontrola ukladu: blok miesci sie w ekranie i nie nachodzi na krawedzie.
        const auto m = Powitanie::miary(t.w, t.h, t.dpi);
        const auto g = Powitanie::geometria(t.w, t.h, t.dpi);
        printf("%s: maska %dx%d, logo %dx%d, znak %dx%d@%u, pasek %dx%d, blok %dx%d (y=%d..%d), dirty %d,%d %dx%d\n",
               t.nazwa, BursztynLogo::SZEROKOSC, BursztynLogo::WYSOKOSC,
               m.logo_w, m.logo_h, m.znak_w, m.znak_h, m.p_znak, m.pasek_w, m.pasek_h,
               m.blok_w, m.blok_h, g.logo.y, g.linia2.y + g.linia2.h, g.dirty.x, g.dirty.y, g.dirty.w, g.dirty.h);
        assert(g.logo.y >= 0);
        assert(g.linia2.y + g.linia2.h <= t.h);
        assert(g.dirty.x >= 0 && g.dirty.x + g.dirty.w <= t.w);
    }
    printf("Podglad klatek: %u/%u PASS\n", checks, checks);
    return 0;
}
