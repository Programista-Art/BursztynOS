// Generator masek napisow ekranu powitalnego (narzedzie hosta, nie trafia do jadra).
//
// Jadro nie parsuje TTF/OTF w Ring 0 (bsf/config_kernel.inc: "No font bytes or TTF
// tables in Ring 0"), a ekran powitalny startuje zanim system plikow, sterta
// uzytkownika i BSF Ring 3 sa dostepne. Dlatego prawdziwe kontury TrueType/OpenType
// sa rasteryzowane podczas budowania tym samym rasteryzatorem, ktorego uzywa BSF
// (bsf/vendor/stb_truetype.h), z rzeczywistymi szerokosciami glifow, kerningiem
// tabeli 'kern' (jesli czcionka ja ma) i pozycjonowaniem podpikselowym. Wynikiem
// sa maski pokrycia 8-bit w .rodata, ktore renderer jadra jedynie zmniejsza filtrem
// obszarowym (bez FPU, bez sterty) do rozmiaru wynikajacego z rozdzielczosci.
//
// Uzycie:
//   g++ -std=c++17 -O2 -I. narzedzia/generuj_napisy_powitania.cpp -o gen
//   ./gen --czcionka czcionki/otf/NotoSans-Regular.otf --wyjscie jadro/gui/napisy_powitania_dane.h
//
// Wyjscie jest deterministyczne: ta sama czcionka daje identyczny naglowek.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "bsf/vendor/stb_truetype.h"

namespace {

// Napisy i ich rozmiar (em w pikselach) przy NAJWIEKSZEJ skali ukladu
// (SKALA_MASEK w promilach). Renderer zawsze zmniejsza, nigdy nie powieksza,
// wiec krawedzie pozostaja gladkie w kazdej rozdzielczosci.
constexpr int SKALA_MASEK = 2000;
struct Napis { const char* nazwa; const char* tekst; int em_px; };
const Napis NAPISY[] = {
    {"TYTUL", "Bursztyn OS", 88},
    {"LINIA1", "Polski system operacyjny", 40},
    {"LINIA2", "Niezale\xC5\xBCny system", 31},
};

uint32_t fnv1a(const std::vector<uint8_t>& d) {
    uint32_t h = 0x811C9DC5u;
    for (uint8_t b : d) h = (h ^ b) * 0x01000193u;
    return h;
}

std::vector<uint32_t> utf8(const char* s) {
    std::vector<uint32_t> out;
    const auto* p = reinterpret_cast<const unsigned char*>(s);
    while (*p) {
        uint32_t cp = *p;
        int n = 0;
        if (cp >= 0xF0) { cp &= 7; n = 3; }
        else if (cp >= 0xE0) { cp &= 15; n = 2; }
        else if (cp >= 0xC0) { cp &= 31; n = 1; }
        ++p;
        for (int i = 0; i < n; ++i) {
            if ((*p & 0xC0) != 0x80) { std::fprintf(stderr, "zly UTF-8\n"); std::exit(2); }
            cp = (cp << 6) | (*p++ & 63);
        }
        out.push_back(cp);
    }
    return out;
}

struct Wynik {
    std::vector<uint8_t> piksele;
    int w = 0, h = 0;
    int linia_bazowa = 0;  // wiersz linii bazowej wzgledem gornej krawedzi maski
    int wersalik = 0;      // wysokosc wersalika (H) nad linia bazowa, px
    int wysokosc_x = 0;    // wysokosc litery x nad linia bazowa, px
};

Wynik rasteryzuj(const stbtt_fontinfo& f, const Napis& n) {
    const float skala = stbtt_ScaleForMappingEmToPixels(&f, float(n.em_px));
    int asc = 0, desc = 0, gap = 0;
    stbtt_GetFontVMetrics(&f, &asc, &desc, &gap);
    const auto cps = utf8(n.tekst);
    for (uint32_t cp : cps)
        if (cp != ' ' && !stbtt_FindGlyphIndex(&f, int(cp))) {
            std::fprintf(stderr, "czcionka nie ma glifu U+%04X (%s)\n", cp, n.nazwa);
            std::exit(3);
        }
    // Plotno z zapasem; linia bazowa w stalym wierszu.
    const int margines = n.em_px;
    const int baza = int(std::ceil(asc * skala)) + margines;
    const int wys = baza + int(std::ceil(-desc * skala)) + margines;
    float x_pen = float(margines);
    std::vector<float> x_glifu(cps.size());
    for (size_t i = 0; i < cps.size(); ++i) {
        x_glifu[i] = x_pen;
        int adv = 0, lsb = 0;
        stbtt_GetCodepointHMetrics(&f, int(cps[i]), &adv, &lsb);
        x_pen += adv * skala;
        if (i + 1 < cps.size())
            x_pen += stbtt_GetCodepointKernAdvance(&f, int(cps[i]), int(cps[i + 1])) * skala;
    }
    const int szer = int(std::ceil(x_pen)) + margines;
    std::vector<uint16_t> akumulator(size_t(szer) * wys, 0);
    std::vector<uint8_t> glif;
    for (size_t i = 0; i < cps.size(); ++i) {
        if (cps[i] == ' ') continue;
        const float xf = x_glifu[i];
        const int xi = int(std::floor(xf));
        const float przesun = xf - float(xi);
        int x0, y0, x1, y1;
        stbtt_GetCodepointBitmapBoxSubpixel(&f, int(cps[i]), skala, skala, przesun, 0.f, &x0, &y0, &x1, &y1);
        const int gw = x1 - x0, gh = y1 - y0;
        if (gw <= 0 || gh <= 0) continue;
        glif.assign(size_t(gw) * gh, 0);
        stbtt_MakeCodepointBitmapSubpixel(&f, glif.data(), gw, gh, gw, skala, skala, przesun, 0.f, int(cps[i]));
        for (int y = 0; y < gh; ++y)
            for (int x = 0; x < gw; ++x) {
                const int dx = xi + x0 + x, dy = baza + y0 + y;
                if (dx < 0 || dy < 0 || dx >= szer || dy >= wys) { std::fprintf(stderr, "poza plotnem\n"); std::exit(4); }
                uint16_t& a = akumulator[size_t(dy) * szer + dx];
                a = uint16_t(a + glif[size_t(y) * gw + x] > 255 ? 255 : a + glif[size_t(y) * gw + x]);
            }
    }
    // Przyciecie do atramentu.
    int mx0 = szer, my0 = wys, mx1 = -1, my1 = -1;
    for (int y = 0; y < wys; ++y)
        for (int x = 0; x < szer; ++x)
            if (akumulator[size_t(y) * szer + x]) {
                if (x < mx0) mx0 = x;
                if (x > mx1) mx1 = x;
                if (y < my0) my0 = y;
                if (y > my1) my1 = y;
            }
    if (mx1 < 0) { std::fprintf(stderr, "pusty napis %s\n", n.nazwa); std::exit(5); }
    Wynik r;
    r.w = mx1 - mx0 + 1;
    r.h = my1 - my0 + 1;
    r.linia_bazowa = baza - my0;
    r.piksele.resize(size_t(r.w) * r.h);
    for (int y = 0; y < r.h; ++y)
        for (int x = 0; x < r.w; ++x)
            r.piksele[size_t(y) * r.w + x] = uint8_t(akumulator[size_t(y + my0) * szer + x + mx0]);
    int bx0, by0, bx1, by1;
    stbtt_GetCodepointBox(&f, 'H', &bx0, &by0, &bx1, &by1);
    r.wersalik = int(std::lround(by1 * skala));
    stbtt_GetCodepointBox(&f, 'x', &bx0, &by0, &bx1, &by1);
    r.wysokosc_x = int(std::lround(by1 * skala));
    return r;
}

std::string nazwa_czcionki(const stbtt_fontinfo& f, int id) {
    int len = 0;
    const char* s = stbtt_GetFontNameString(&f, &len, STBTT_PLATFORM_ID_MICROSOFT,
                                            STBTT_MS_EID_UNICODE_BMP, STBTT_MS_LANG_ENGLISH, id);
    std::string out;
    if (!s) return out;
    for (int i = 0; i + 1 < len; i += 2) {  // UTF-16BE, nazwy ASCII
        const char c = s[i + 1];
        out += (s[i] == 0 && c >= 32 && c < 127 && c != '"' && c != '\\') ? c : '?';
    }
    return out;
}

}  // namespace

int main(int argc, char** argv) {
    const char* czcionka = nullptr;
    const char* wyjscie = nullptr;
    for (int i = 1; i + 1 < argc; i += 2) {
        if (!std::strcmp(argv[i], "--czcionka")) czcionka = argv[i + 1];
        else if (!std::strcmp(argv[i], "--wyjscie")) wyjscie = argv[i + 1];
    }
    if (!czcionka || !wyjscie) {
        std::fprintf(stderr, "uzycie: %s --czcionka PLIK.ttf|otf --wyjscie NAGLOWEK.h\n", argv[0]);
        return 1;
    }
    FILE* in = std::fopen(czcionka, "rb");
    if (!in) { std::perror(czcionka); return 1; }
    std::vector<uint8_t> dane;
    for (int c; (c = std::fgetc(in)) != EOF;) dane.push_back(uint8_t(c));
    std::fclose(in);
    stbtt_fontinfo f;
    if (!stbtt_InitFont(&f, dane.data(), stbtt_GetFontOffsetForIndex(dane.data(), 0))) {
        std::fprintf(stderr, "niepoprawna czcionka %s\n", czcionka);
        return 1;
    }
    // Sciezka w naglowku bez katalogu roboczego (determinizm niezalezny od checkoutu).
    std::string zrodlo = czcionka;
    const size_t poz = zrodlo.find("czcionki/");
    if (poz != std::string::npos) zrodlo = zrodlo.substr(poz);

    std::string out;
    char bufor[256];
    out += "#pragma once\n#include <stdint.h>\n\n";
    out += "// PLIK GENEROWANY przez narzedzia/generuj_napisy_powitania.cpp -- nie edytowac.\n";
    out += "// Zrodlo: " + zrodlo + " (" + nazwa_czcionki(f, 4) + ", " + nazwa_czcionki(f, 5) + ").\n";
    out += "// Maski pokrycia napisow ekranu powitalnego zrasteryzowane z konturow czcionki\n";
    out += "// rasteryzatorem BSF (stb_truetype): rzeczywiste szerokosci glifow, pozycje\n";
    out += "// podpikselowe, bez rozstrzelenia i bez skalowania poziomego. Renderer jadra\n";
    out += "// zmniejsza je filtrem obszarowym do rozmiaru wynikajacego z ukladu.\n";
    out += "namespace NapisyPowitania {\n";
    std::snprintf(bufor, sizeof bufor, "inline constexpr uint32_t SUMA_KONTROLNA_CZCIONKI = 0x%08Xu;\n", fnv1a(dane));
    out += bufor;
    std::snprintf(bufor, sizeof bufor, "inline constexpr int SKALA_MASEK = %d; // promile skali ukladu, przy ktorej maska ma 1:1\n", SKALA_MASEK);
    out += bufor;
    out += "struct Maska {\n"
           "    const uint8_t* piksele;\n"
           "    int w, h;\n"
           "    int linia_bazowa; // wiersz linii bazowej od gornej krawedzi maski\n"
           "    int wersalik;     // wysokosc wersalika nad linia bazowa\n"
           "    int wysokosc_x;   // wysokosc litery x nad linia bazowa\n"
           "    int em;           // rozmiar czcionki (em) w pikselach\n"
           "};\n";
    size_t lacznie = 0;
    for (const auto& n : NAPISY) {
        const Wynik r = rasteryzuj(f, n);
        lacznie += r.piksele.size();
        std::snprintf(bufor, sizeof bufor, "// \"%s\": %dx%d px, em %d px\n", n.tekst, r.w, r.h, n.em_px);
        out += bufor;
        std::snprintf(bufor, sizeof bufor, "inline constexpr uint8_t %s_PIKSELE[%zu] = {\n", n.nazwa, r.piksele.size());
        out += bufor;
        for (size_t i = 0; i < r.piksele.size(); ++i) {
            std::snprintf(bufor, sizeof bufor, "%s%u,", (i % 32) ? "" : "    ", r.piksele[i]);
            out += bufor;
            if (i % 32 == 31 || i + 1 == r.piksele.size()) out += "\n";
        }
        out += "};\n";
        std::snprintf(bufor, sizeof bufor,
                      "inline constexpr Maska %s{%s_PIKSELE, %d, %d, %d, %d, %d, %d};\n",
                      n.nazwa, n.nazwa, r.w, r.h, r.linia_bazowa, r.wersalik, r.wysokosc_x, n.em_px);
        out += bufor;
    }
    std::snprintf(bufor, sizeof bufor, "inline constexpr unsigned BAJTY_MASEK = %zu;\n", lacznie);
    out += bufor;
    out += "}  // namespace NapisyPowitania\n";
    FILE* o = std::fopen(wyjscie, "wb");
    if (!o) { std::perror(wyjscie); return 1; }
    std::fwrite(out.data(), 1, out.size(), o);
    std::fclose(o);
    std::printf("%s: %zu B masek z %s\n", wyjscie, lacznie, zrodlo.c_str());
    return 0;
}
