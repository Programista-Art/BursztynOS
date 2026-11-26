// Host (ASan/UBSan): skalowanie i kompozycja ikon PNG z kanalem alfa (obrazy/skalowanie.h: skaluj_ikone_alfa,
// wspolne dla BursztynUI::IkonaPNG - pulpit, pasek zadan, menu Start, menu - i BursztynUI::ObrazIkony - Grajek).
//
// Regresja bialych obwodek: grafiki z edytorow maja pod A=0 bialy RGB (FFFFFF). Dawne skalowanie IkonaPNG
// (skaluj_obraz_bilinear) interpolowalo kanaly niezaleznie, wiec ten bialy kolor przeciekal na wygladzone
// krawedzie. Test sprawdza na syntetycznych i na PRAWDZIWYCH ikonach repozytorium:
//  - piksel w pelni przezroczysty pozostaje niewidoczny (0 po skalowaniu, tlo bez zmian po kompozycji),
//  - kolor wynikowy lezy w zakresie kolorow WIDOCZNYCH pikseli zrodla pod nim (zadnego koloru spod A=0),
//  - wynik zgodny z niezaleznym wzorcem zmiennoprzecinkowym (srednia obszarowa na premnozonej alfie),
//  - kompozycja na ciemnym, jasnym i kolorowym tle (kolor_nad jadra/BursztynUI) bez obwodek,
//  - rozmiary 128 (kopia 1:1) .. 8 px, powiekszanie, proporcje nie-kwadratowe, powtarzalnosc i niezmiennosc zrodla.
//
//   test_ikony_alfa_host <katalog ikon> [<katalog ikon> ...]
#include "obrazy/skalowanie.h"
#include "obrazy/dekoder_obrazu.h"
#include "jadro/gui/kolor_alfa.h"
#include <dirent.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

static unsigned checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); std::abort(); } } while (0)

using Piksele = std::vector<uint32_t>;
static unsigned kanal(uint32_t c, int i) { return (c >> (16 - 8 * i)) & 0xFFu; }
static int jasnosc(uint32_t c) { return int(kanal(c, 0) * 54 + kanal(c, 1) * 183 + kanal(c, 2) * 19) >> 8; }

static Piksele skaluj(const Piksele& z, uint32_t sw, uint32_t sh, uint32_t bok) {
    Piksele d(size_t(bok) * bok, 0xDEADBEEFu);
    CHECK(skaluj_ikone_alfa(z.data(), sw, sh, z.size() * 4, d.data(), bok, bok, bok, d.size() * 4));
    return d;
}

// Niezalezny wzorzec (double): ta sama geometria Dopasuj; pomniejszanie = srednia obszarowa, powiekszanie =
// namiot; premnozona alfa. Zwraca A i RGB (proste) w double.
struct Wzor { double a, r, g, b; };
static Wzor wzorzec(const Piksele& z, uint32_t sw, uint32_t sh, uint32_t bok, uint32_t x, uint32_t y, GeometriaObrazu g,
                    double* rmin = nullptr, double* rmax = nullptr) {
    (void)bok;
    if (x < g.dst_x || y < g.dst_y || x >= g.dst_x + g.dst_w || y >= g.dst_y + g.dst_h) return {0, 0, 0, 0};
    const double lx = x - g.dst_x, ly = y - g.dst_y;
    auto os = [](double d, uint32_t zr, uint32_t cel, std::vector<std::pair<uint32_t, double>>& w) {
        w.clear();
        if (zr >= cel) {
            const double a = d * zr / cel, b = (d + 1) * zr / cel;
            for (uint32_t i = uint32_t(std::floor(a)); i < zr && i < b; ++i) {
                const double p = std::fmin(b, i + 1.0) - std::fmax(a, double(i));
                if (p > 1e-12) w.push_back({i, p});
            }
        } else {
            double c = (d + 0.5) * zr / cel - 0.5;
            if (c < 0) c = 0;
            const uint32_t i0 = uint32_t(c), i1 = i0 + 1 < zr ? i0 + 1 : i0;
            const double f = c - i0;
            w.push_back({i0, 1 - f});
            if (f > 0) w.push_back({i1, f});
        }
    };
    std::vector<std::pair<uint32_t, double>> wx, wy;
    os(lx, sw, g.dst_w, wx); os(ly, sh, g.dst_h, wy);
    double sa = 0, sr = 0, sg = 0, sb = 0, sw_ = 0;
    if (rmin) for (int i = 0; i < 3; ++i) { rmin[i] = 1e9; rmax[i] = -1e9; }
    for (auto [iy, fy] : wy)
        for (auto [ix, fx] : wx) {
            const uint32_t c = z[size_t(iy) * sw + ix];
            const double a = (c >> 24) / 255.0, w = fx * fy;
            sw_ += w;
            if (!(c >> 24)) continue;
            sa += w * a; sr += w * a * kanal(c, 0); sg += w * a * kanal(c, 1); sb += w * a * kanal(c, 2);
            if (rmin) for (int i = 0; i < 3; ++i) { rmin[i] = std::fmin(rmin[i], kanal(c, i)); rmax[i] = std::fmax(rmax[i], kanal(c, i)); }
        }
    if (sa <= 0) return {0, 0, 0, 0};
    return {255.0 * sa / sw_, sr / sa, sg / sa, sb / sa};
}

static const uint32_t TLA[3] = {0xFF202124u, 0xFFF7F6F3u, 0xFF1E5AA8u};   // ciemne (motyw nocny), jasne, kolorowe
static const char* NAZWY_TLA[3] = {"ciemne", "jasne", "kolorowe"};

// Zlozenie wzorca na tle (double, straight alpha over).
static uint32_t nad_wzor(uint32_t tlo, const Wzor& w) {
    const double a = w.a / 255.0;
    const double k[3] = {w.r, w.g, w.b};
    uint32_t o = 0xFF000000u;
    for (int i = 0; i < 3; ++i) o |= uint32_t(std::lround(k[i] * a + kanal(tlo, i) * (1 - a))) << (16 - 8 * i);
    return o;
}

struct Wynik { unsigned poza_zakresem = 0, max_da = 0, max_dc = 0, max_dkomp = 0, piksele = 0; };

// Pelna kontrola jednego przeskalowania wzgledem wzorca; `wynik` zbiera odchylki.
static Wynik sprawdz(const Piksele& z, uint32_t sw, uint32_t sh, uint32_t bok, const Piksele& d) {
    GeometriaObrazu g{};
    CHECK(geometria_obrazu(sw, sh, bok, bok, TrybSkalowaniaObrazu::Dopasuj, &g));
    Wynik w;
    for (uint32_t y = 0; y < bok; ++y)
        for (uint32_t x = 0; x < bok; ++x) {
            const uint32_t c = d[size_t(y) * bok + x];
            double mn[3], mx[3];
            const Wzor r = wzorzec(z, sw, sh, bok, x, y, g, mn, mx);
            ++w.piksele;
            if (!(c >> 24)) { CHECK(c == 0); }                         // niewidoczny = dokladnie 0 (bez koloru)
            const unsigned da = unsigned(std::fabs(double(c >> 24) - r.a) + 0.5);
            if (da > w.max_da) w.max_da = da;
            if ((c >> 24) && r.a > 0) {
                // zakres kolorow widocznych pikseli pod spodem (bez koloru spod A=0) - z tolerancja zaokraglenia
                for (int i = 0; i < 3; ++i)
                    if (kanal(c, i) + 1 < mn[i] || kanal(c, i) > mx[i] + 1) ++w.poza_zakresem;
                if ((c >> 24) >= 16) {
                    const double k[3] = {r.r, r.g, r.b};
                    for (int i = 0; i < 3; ++i) {
                        const unsigned dc = unsigned(std::fabs(kanal(c, i) - k[i]) + 0.5);
                        if (dc > w.max_dc) w.max_dc = dc;
                    }
                }
            }
            for (uint32_t t : TLA) {
                const uint32_t o = kolor_nad(t, c), e = nad_wzor(t, r);
                if (!(c >> 24)) CHECK(o == t);                          // przezroczysty: tlo bez zmian
                for (int i = 0; i < 3; ++i) {
                    const unsigned dk = unsigned(std::abs(int(kanal(o, i)) - int(kanal(e, i))));
                    if (dk > w.max_dkomp) w.max_dkomp = dk;
                }
            }
        }
    return w;
}

// Syntetyczna ikona: ciemnoczerwone kolo z wygladzona krawedzia + polprzezroczysty pasek, tlo A=0 z BIALYM RGB.
static Piksele syntetyczna(uint32_t w, uint32_t h) {
    Piksele z(size_t(w) * h);
    const double cx = w / 2.0, cy = h / 2.0, rr = (w < h ? w : h) * 0.38;
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x) {
            const double d = std::hypot(x + 0.5 - cx, y + 0.5 - cy);
            double a = std::fmin(1.0, std::fmax(0.0, rr + 0.5 - d));            // krawedz ~1 px
            uint32_t rgb = 0x40100Bu;
            if (y >= h * 3 / 4 && y < h * 3 / 4 + 4) { a = std::fmax(a, 0.5); rgb = 0xC08030u; }   // pasek 50 %
            const uint32_t A = uint32_t(std::lround(a * 255));
            z[size_t(y) * w + x] = A ? (A << 24) | rgb : 0x00FFFFFFu;         // bialy kolor pod A=0
        }
    return z;
}

static bool wczytaj_png(const std::string& p, Piksele* o, uint32_t* w, uint32_t* h) {
    std::ifstream f(p, std::ios::binary);
    const std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), {});
    DekodowanyObraz dek{};
    if (d.empty() || dekoduj_ikone_png(d.data(), d.size(), &dek, std::malloc, std::free) != BladObrazu::OK) return false;
    *w = dek.szerokosc; *h = dek.wysokosc;
    o->assign(size_t(*w) * *h, 0);
    for (uint32_t y = 0; y < *h; ++y) std::memcpy(o->data() + size_t(y) * *w, dek.piksele + size_t(y) * dek.stride, size_t(*w) * 4);
    std::free(dek.piksele);
    return true;
}

static const uint32_t BOKI[] = {128, 96, 64, 48, 36, 32, 24, 22, 20, 16, 8};

int main(int argc, char** argv) {
    // 1. Syntetyczne: przezroczystosc, wygladzone krawedzie, wszystkie rozmiary + powiekszanie + nie-kwadrat.
    {
        const Piksele z = syntetyczna(128, 128);
        const Piksele kopia = z;
        for (uint32_t bok : BOKI) {
            const Piksele d = skaluj(z, 128, 128, bok);
            const Wynik w = sprawdz(z, 128, 128, bok, d);
            CHECK(w.poza_zakresem == 0 && w.max_da <= 1 && w.max_dc <= 1 && w.max_dkomp <= 2);
            CHECK(d[0] == 0 && d[bok * bok - 1] == 0);                                  // narozniki przezroczyste
            CHECK(skaluj(z, 128, 128, bok) == d);                                       // powtarzalnosc
        }
        CHECK(z == kopia);                                                              // zrodlo niezmienione
        // 128 -> 128: kopia 1:1 widocznych pikseli, przezroczyste = 0
        const Piksele d = skaluj(z, 128, 128, 128);
        for (size_t i = 0; i < z.size(); ++i) CHECK(d[i] == ((z[i] >> 24) ? z[i] : 0u));
        // powiekszanie (32 -> 48, 96) i proporcje 128x64 / 40x128
        const Piksele m = syntetyczna(32, 32);
        for (uint32_t bok : {48u, 96u, 128u}) {
            const Wynik w = sprawdz(m, 32, 32, bok, skaluj(m, 32, 32, bok));
            CHECK(w.poza_zakresem == 0 && w.max_da <= 1 && w.max_dc <= 1 && w.max_dkomp <= 2);
        }
        for (auto [sw, sh] : {std::pair<uint32_t, uint32_t>{128, 64}, {40, 128}}) {
            const Piksele n = syntetyczna(sw, sh);
            for (uint32_t bok : {48u, 22u}) {
                const Piksele d2 = skaluj(n, sw, sh, bok);
                const Wynik w = sprawdz(n, sw, sh, bok, d2);
                CHECK(w.poza_zakresem == 0 && w.max_da <= 1 && w.max_dkomp <= 2);
            }
        }
        // calkowicie przezroczysta ikona (bialy RGB pod A=0): wynik 0 wszedzie
        const Piksele pusta(128 * 128, 0x00FFFFFFu);
        for (uint32_t bok : BOKI) for (uint32_t c : skaluj(pusta, 128, 128, bok)) CHECK(c == 0);
        // bledne argumenty: bez zapisu poza buforem
        Piksele maly(4, 0);
        CHECK(!skaluj_ikone_alfa(z.data(), 128, 128, z.size() * 4, maly.data(), 4, 4, 4, maly.size() * 4));
        CHECK(!skaluj_ikone_alfa(nullptr, 128, 128, 0, maly.data(), 2, 2, 2, maly.size() * 4));
        std::printf("1. syntetyczne: %zu rozmiarow, powiekszanie, proporcje, pelna przezroczystosc - OK\n", sizeof(BOKI) / sizeof(BOKI[0]));
    }
    // 2. Przyczyna: dawne skalowanie IkonaPNG (kanaly niezaleznie) przenosi bialy RGB spod A=0 na krawedz.
    {
        const Piksele z = syntetyczna(128, 128);
        GeometriaObrazu g{};
        CHECK(geometria_obrazu(128, 128, 22, 22, TrybSkalowaniaObrazu::Dopasuj, &g));
        Piksele stare(22 * 22, 0);
        CHECK(skaluj_obraz_bilinear(z.data(), 128, 128, z.size() * 4, stare.data(), 22, 22, 22, stare.size() * 4, TrybSkalowaniaObrazu::Dopasuj, 0));
        const Piksele nowe = skaluj(z, 128, 128, 22);
        int stara_max = 0, nowa_max = 0;
        for (size_t i = 0; i < nowe.size(); ++i) {
            const Wzor r = wzorzec(z, 128, 128, 22, uint32_t(i % 22), uint32_t(i / 22), g);
            const int e = jasnosc(nad_wzor(TLA[0], r));
            stara_max = std::max(stara_max, jasnosc(kolor_nad(TLA[0], stare[i])) - e);
            nowa_max = std::max(nowa_max, jasnosc(kolor_nad(TLA[0], nowe[i])) - e);
        }
        CHECK(stara_max > 40 && nowa_max <= 2);
        std::printf("2. dawne skalowanie (kanaly niezaleznie): krawedz jasniejsza o %d (biala obwodka na ciemnym tle), "
                    "nowe: %d\n", stara_max, nowa_max);
    }
    // 3. Prawdziwe ikony repozytorium (Grajek: grajek_zasoby = oryginaly uzytkownika; pulpit/pasek/Start: ikonki).
    unsigned ikon = 0, skalowan = 0;
    Wynik suma;
    for (int k = 1; k < argc; ++k) {
        DIR* dir = opendir(argv[k]);
        CHECK(dir != nullptr);
        std::vector<std::string> pliki;
        while (dirent* e = readdir(dir)) {
            const std::string n = e->d_name;
            if (n.size() > 4 && n.compare(n.size() - 4, 4, ".png") == 0) pliki.push_back(std::string(argv[k]) + "/" + n);
        }
        closedir(dir);
        for (const auto& p : pliki) {
            Piksele z; uint32_t w = 0, h = 0;
            if (!wczytaj_png(p, &z, &w, &h)) { std::fprintf(stderr, "pominieto (dekoder ikon): %s\n", p.c_str()); continue; }
            ++ikon;
            unsigned przezroczyste = 0;
            for (uint32_t c : z) przezroczyste += !(c >> 24);
            for (uint32_t bok : BOKI) {
                const Piksele d = skaluj(z, w, h, bok);
                const Wynik r = sprawdz(z, w, h, bok, d);
                const unsigned tol_c = 1;
                if (r.poza_zakresem || r.max_da > 1 || r.max_dc > tol_c || r.max_dkomp > 2)
                    std::fprintf(stderr, "%s @%u: poza=%u da=%u dc=%u dkomp=%u\n", p.c_str(), bok, r.poza_zakresem, r.max_da, r.max_dc, r.max_dkomp);
                CHECK(r.poza_zakresem == 0 && r.max_da <= 1 && r.max_dc <= tol_c && r.max_dkomp <= 2);
                suma.piksele += r.piksele; ++skalowan;
                if (bok == w && bok == h) for (size_t i = 0; i < z.size(); ++i) CHECK(d[i] == ((z[i] >> 24) ? z[i] : 0u));
            }
            (void)przezroczyste;
        }
    }
    CHECK(argc < 2 || ikon > 0);
    std::printf("3. prawdziwe ikony: %u plikow x %zu rozmiarow (%u skalowan, %u pikseli): bez koloru spod A=0, "
                "zgodne ze wzorcem (alfa +-1, kolor +-1, kompozycja +-2 na tle: %s/%s/%s)\n",
                ikon, sizeof(BOKI) / sizeof(BOKI[0]), skalowan, suma.piksele, NAZWY_TLA[0], NAZWY_TLA[1], NAZWY_TLA[2]);
    std::printf("test_ikony_alfa_host: %u sprawdzen OK\n", checks);
    return 0;
}
