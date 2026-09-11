/* Hostowy test parsera formatu aktualizacji (ten sam naglowek co jadro).
 * Kompilacja: g++ -std=c++17 -Wall -Wextra -o test_format_host test_format_host.cpp
 * Zwraca 0 gdy wszystkie kontrole przeszly, 1 w przeciwnym razie.
 * Kazda kontrola wypisuje jedna linie: PASS/FAIL nazwa.
 */
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>

#include "../../aktualizacja_format.h"

static int zdane = 0;
static int zlane = 0;

static void raport(const char* nazwa, bool ok, bool oczekiwane_ok) {
    bool zgodne = (ok == oczekiwane_ok);
    if (zgodne) ++zdane;
    else ++zlane;
    std::printf("%s %s\n", zgodne ? "PASS" : "FAIL", nazwa);
}

static std::string zloty_wykaz(const char* podpis_hex) {
    std::string s;
    s += "wersjaformatu=1\n";
    s += "wersjasystemu=0.2.0\n";
    s += "minimalnawersja=0.1.0\n";
    s += "plik=bursztyn-0.2.0.akt\n";
    s += "rozmiar=566\n";
    s += "sumasha256=3ccc093924026f1e757399d44f82c469636202f2a880bb707ebd0bb345cbddd4\n";
    s += "podpis=";
    s += podpis_hex;
    s += "\n";
    s += "kanal=stabilny\n";
    return s;
}

static std::string wczytaj_podpis_z_wektora() {
    /* Podpis z aktualizacja.cpp (wektor testowy 0.2.0). */
    return "39c3ae4c10e18066b44dc03972e7486a957251c05953c363a581b72c9c90902421"
           "14c7923bcd949b30eaf9168a54d7eb508cb7336d78baf1472c397f1a25a35defe12"
           "f1a55f23f2c27d3f414d69734fa8b8f5e222ddbac9f5caed18535458875df74b562"
           "55a1d4080dfe8e984e17f00fef40bbfb4f86b9aa50be04cc73815afb86683f3a8fc"
           "7c475d333bd05cd3f548516297c558496f0644a09188d1b37c9f8613d6dccd46293"
           "e752e74aeb0545bae2971e2d10060993c84bcd1d8f60dffc14c2cd8ee9afb2f1271"
           "26d85d6e4b58c6ea23fc1fc3af1df2de65276ace34222f5887882d0fa511647b4d0"
           "2407e9225b8ba5a381e8e5af01ee0d222645dc0afdd0";
}

int main() {
    akt_wykaz_t w = {};
    std::string podpis = wczytaj_podpis_z_wektora();
    std::string zloty = zloty_wykaz(podpis.c_str());

    raport("wykaz-poprawny", akt_parsuj_wykaz(zloty.c_str(), zloty.size(), &w) == AKT_OK, true);
    raport("wykaz-pusty", akt_parsuj_wykaz("", 0, &w) == AKT_OK, false);
    {
        std::string s = zloty + "kanal=stabilny\n";
        raport("wykaz-duplikat", akt_parsuj_wykaz(s.c_str(), s.size(), &w) == AKT_OK, false);
    }
    {
        std::string s = "wersjaformatu=1\nwersjasystemu=0.2.0\n";
        raport("wykaz-brak-pol", akt_parsuj_wykaz(s.c_str(), s.size(), &w) == AKT_OK, false);
    }
    {
        std::string s = zloty;
        size_t p = s.find("wersjaformatu=1");
        s.replace(p, 15, "wersjaformatu=99");
        raport("wykaz-zly-format", akt_parsuj_wykaz(s.c_str(), s.size(), &w) == AKT_OK, false);
    }
    {
        std::string s = zloty;
        size_t p = s.find("wersjasystemu=0.2.0");
        s.replace(p, 19, "wersjasystemu=1.2");
        raport("wykaz-zla-wersja", akt_parsuj_wykaz(s.c_str(), s.size(), &w) == AKT_OK, false);
    }
    {
        std::string s = zloty;
        size_t p = s.find("kanal=stabilny");
        s.replace(p, 14, "kanal=nieznany");
        raport("wykaz-zly-kanal", akt_parsuj_wykaz(s.c_str(), s.size(), &w) == AKT_OK, false);
    }
    {
        std::string s = zloty;
        size_t p = s.find("rozmiar=566");
        s.replace(p, 11, "rozmiar=99999999999999999999999");
        raport("wykaz-przepelnienie-rozmiaru", akt_parsuj_wykaz(s.c_str(), s.size(), &w) == AKT_OK, false);
    }
    {
        std::string s = zloty;
        size_t p = s.find("sumasha256=");
        s.replace(p + 11, 64, std::string(64, 'z'));
        raport("wykaz-zly-hash", akt_parsuj_wykaz(s.c_str(), s.size(), &w) == AKT_OK, false);
    }
    {
        std::string s = zloty;
        s.resize(s.size() - 1); /* bez koncowego LF */
        raport("wykaz-bez-lf", akt_parsuj_wykaz(s.c_str(), s.size(), &w) == AKT_OK, false);
    }
    {
        std::string s = zloty;
        s += "nieznane=1\n";
        raport("wykaz-nieznane-pole", akt_parsuj_wykaz(s.c_str(), s.size(), &w) == AKT_OK, false);
    }
    {
        char nul_data[] = {'w', 'e', 'r', 's', 'j', 'a', '\0', 'x', '\n'};
        raport("wykaz-nul", akt_parsuj_wykaz(nul_data, sizeof(nul_data), &w) == AKT_OK, false);
    }
    {
        std::string s = zloty;
        s.insert(0, "\xEF\xBB\xBF");
        raport("wykaz-bom", akt_parsuj_wykaz(s.c_str(), s.size(), &w) == AKT_OK, false);
    }
    {
        std::string s = zloty;
        size_t p = s.find("plik=bursztyn-0.2.0.akt");
        s.replace(p, 23, "plik=/etc/passwd");
        raport("wykaz-sciezka-absolutna", akt_parsuj_wykaz(s.c_str(), s.size(), &w) == AKT_OK, false);
    }
    {
        std::string s = zloty;
        size_t p = s.find("plik=bursztyn-0.2.0.akt");
        s.replace(p, 23, "plik=../ukryty.akt");
        raport("wykaz-traversal", akt_parsuj_wykaz(s.c_str(), s.size(), &w) == AKT_OK, false);
    }
    {
        std::string s = zloty;
        s.replace(s.find("rozmiar=566"), 11, "rozmiar=0");
        /* rozmiar 0 odrzucany; ale tez nazwa/rozmiar niespojne - parser odrzuci */
        raport("wykaz-rozmiar-zero", akt_parsuj_wykaz(s.c_str(), s.size(), &w) == AKT_OK, false);
    }
    {
        std::string s = zloty;
        size_t p = s.find("minimalnawersja=0.1.0");
        s.replace(p, 21, "minimalnawersja=0.3.0");
        raport("wykaz-minimalna-wyzsza-niz-systemu", akt_parsuj_wykaz(s.c_str(), s.size(), &w) == AKT_OK, false);
    }
    {
        std::string s = zloty;
        size_t p = s.find("minimalnawersja=0.1.0");
        s.replace(p, 21, "minimalnawersja=invalid");
        raport("wykaz-zla-minimalna-wersja", akt_parsuj_wykaz(s.c_str(), s.size(), &w) == AKT_OK, false);
    }
    {
        std::string s = zloty;
        size_t p = s.find("podpis=");
        s.replace(p, 7 + 512, "podpis=1234");
        raport("wykaz-krotki-podpis", akt_parsuj_wykaz(s.c_str(), s.size(), &w) == AKT_OK, false);
    }
    {
        std::string s = zloty;
        size_t p = s.find("podpis=");
        s.replace(p, 7 + 512, "podpis=" + podpis + "00");
        raport("wykaz-za-dlugi-podpis", akt_parsuj_wykaz(s.c_str(), s.size(), &w) == AKT_OK, false);
    }
    {
        std::string s = zloty;
        size_t p = s.find("podpis=");
        std::string zly_hex = podpis;
        zly_hex[10] = 'z';
        s.replace(p + 7, 512, zly_hex);
        raport("wykaz-non-hex-podpis", akt_parsuj_wykaz(s.c_str(), s.size(), &w) == AKT_OK, false);
    }
    {
        std::string s = zloty;
        size_t p = s.find("podpis=");
        std::string upper_hex = podpis;
        upper_hex[10] = 'A';
        s.replace(p + 7, 512, upper_hex);
        raport("wykaz-uppercase-hex-podpis", akt_parsuj_wykaz(s.c_str(), s.size(), &w) == AKT_OK, false);
    }
    {
        std::string s = zloty.substr(0, 45);
        raport("wykaz-uciety-input", akt_parsuj_wykaz(s.c_str(), s.size(), &w) == AKT_OK, false);
    }
    {
        std::string s = zloty;
        s.append(8193 - s.size(), ' ');
        s += "\n";
        raport("wykaz-oversized-input", akt_parsuj_wykaz(s.c_str(), s.size(), &w) == AKT_OK, false);
    }
    /* Wersje. */
    {
        akt_wersja_t a{0, 1, 0}, b{0, 2, 0}, m{0, 1, 0};
        raport("wersja-dozwolona", akt_czy_dozwolona_aktualizacja(&a, &b, &m) == 1, true);
        raport("wersja-ta-sama", akt_czy_dozwolona_aktualizacja(&b, &b, &m) == 1, false);
        raport("wersja-downgrade", akt_czy_dozwolona_aktualizacja(&b, &a, &m) == 1, false);
        akt_wersja_t v1{0, 9, 0}, v2{0, 10, 0};
        raport("wersja-numeryczna-9<10", akt_porownaj_wersje(&v1, &v2) < 0, true);
        akt_wersja_t z{};
        raport("wersja-zla", akt_parsuj_wersje("1.2", 3, &z) == AKT_OK, false);
        raport("wersja-zla2", akt_parsuj_wersje("a.b.c", 5, &z) == AKT_OK, false);
        raport("wersja-pusta", akt_parsuj_wersje("", 0, &z) == AKT_OK, false);
    }
    /* Pakiet: zbuduj minimalny poprawny w RAM. */
    {
        uint8_t pakiet[16 + 256 + 4];
        memset(pakiet, 0, sizeof(pakiet));
        pakiet[0] = 'B'; pakiet[1] = 'A'; pakiet[2] = 'K'; pakiet[3] = 'T';
        pakiet[4] = 1;
        pakiet[8] = 1;
        uint8_t* wpis = pakiet + 16;
        memcpy(wpis, "konfig", 6);
        memcpy(wpis + 32, "test.txt", 8);
        uint64_t off = 16 + 256;
        for (int i = 0; i < 8; ++i) wpis[32 + 128 + i] = (uint8_t)((off >> (8 * i)) & 0xFF);
        uint64_t roz = 4;
        for (int i = 0; i < 8; ++i) wpis[32 + 128 + 8 + i] = (uint8_t)((roz >> (8 * i)) & 0xFF);
        /* hash: 32 bajty wzorca (nie weryfikujemy kryptograficznie tutaj) */
        for (int i = 0; i < 32; ++i) wpis[32 + 128 + 16 + i] = (uint8_t)i;
        memcpy(pakiet + 16 + 256, "test", 4);
        uint32_t liczba = 0;
        raport("pakiet-poprawny", akt_sprawdz_pakiet(pakiet, sizeof(pakiet), &liczba) == AKT_OK, true);
        uint8_t zla[sizeof(pakiet)];
        memcpy(zla, pakiet, sizeof(pakiet));
        zla[0] = 'X';
        raport("pakiet-zly-magic", akt_sprawdz_pakiet(zla, sizeof(zla), nullptr) == AKT_OK, false);
        memcpy(zla, pakiet, sizeof(pakiet));
        zla[4] = 99;
        raport("pakiet-zly-format", akt_sprawdz_pakiet(zla, sizeof(zla), nullptr) == AKT_OK, false);
        memcpy(zla, pakiet, sizeof(pakiet));
        /* nakladanie: drugi wpis z tym samym offsetem */
        raport("pakiet-uciety", akt_sprawdz_pakiet(pakiet, 16 + 100, nullptr) == AKT_OK, false);
        /* offset poza pakiet */
        memcpy(zla, pakiet, sizeof(pakiet));
        for (int i = 0; i < 8; ++i) zla[16 + 32 + 128 + i] = 0xFF;
        raport("pakiet-offset-poza", akt_sprawdz_pakiet(zla, sizeof(zla), nullptr) == AKT_OK, false);
        /* sciezka absolutna */
        memcpy(zla, pakiet, sizeof(pakiet));
        zla[16 + 32] = '/';
        raport("pakiet-sciezka-absolutna", akt_sprawdz_pakiet(zla, sizeof(zla), nullptr) == AKT_OK, false);
        /* traversal */
        memcpy(zla, pakiet, sizeof(pakiet));
        memcpy(zla + 16 + 32, "../x", 4);
        raport("pakiet-traversal", akt_sprawdz_pakiet(zla, sizeof(zla), nullptr) == AKT_OK, false);
        /* zla kategoria */
        memcpy(zla, pakiet, sizeof(pakiet));
        memcpy(zla + 16, "sekrety", 7);
        raport("pakiet-kategoria", akt_sprawdz_pakiet(zla, sizeof(zla), nullptr) == AKT_OK, false);
    }
    /* Kanoniczne: stabilnosc buildera. */
    {
        akt_wykaz_t wk = {};
        akt_wykaz_t* p = &wk;
        std::string z = zloty;
        if (akt_parsuj_wykaz(z.c_str(), z.size(), p) == AKT_OK) {
            char kan[1024];
            size_t klen = akt_buduj_kanoniczne(p, kan, sizeof(kan));
            const char* oczekiwane =
                "wersjaformatu=1\nwersjasystemu=0.2.0\nminimalnawersja=0.1.0\n"
                "plik=bursztyn-0.2.0.akt\nrozmiar=566\n"
                "sumasha256=3ccc093924026f1e757399d44f82c469636202f2a880bb707ebd0bb345cbddd4\n"
                "kanal=stabilny\n";
            bool rowne = (klen == strlen(oczekiwane) && memcmp(kan, oczekiwane, klen) == 0);
            raport("kanoniczne-stabilne", rowne, true);
        } else {
            raport("kanoniczne-stabilne", false, true);
        }
    }
    std::printf("WYNIK zdane=%d zlane=%d\n", zdane, zlane);
    return zlane == 0 ? 0 : 1;
}
