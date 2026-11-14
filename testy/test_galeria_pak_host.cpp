// Testy hosta: kontener galerii BZP3 (jadro/fs/galeria_pak.h). Parser jest bezpieczny wzgledem
// dowolnych bajtow: prawidlowe kontenery, kazda klasa uszkodzen, przepelnienia, traversal, duplikaty,
// ciagly uklad, fuzz losowy (ASan/UBSan). Opcjonalny argument: sciezka prawdziwego zdjecia.pak (z package_galeria.py).
#include "../jadro/fs/galeria_pak.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
using namespace GaleriaPak;
static int g_fail = 0, g_ok = 0;
#define CHECK(c) do { if (c) ++g_ok; else { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

// --- SHA-256 (referencyjna, tylko do testow) ---
static uint32_t rr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
static bool sha256(const uint8_t* d, uint64_t n, uint8_t out[32]) {
    static const uint32_t K[64] = {0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
    uint32_t h[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    std::vector<uint8_t> m(d, d + n); m.push_back(0x80);
    while (m.size() % 64 != 56) m.push_back(0);
    for (int i = 7; i >= 0; --i) m.push_back(static_cast<uint8_t>((n * 8) >> (i * 8)));
    for (size_t o = 0; o < m.size(); o += 64) {
        uint32_t w[64];
        for (int i = 0; i < 16; ++i) w[i] = (uint32_t(m[o + 4 * i]) << 24) | (uint32_t(m[o + 4 * i + 1]) << 16) | (uint32_t(m[o + 4 * i + 2]) << 8) | m[o + 4 * i + 3];
        for (int i = 16; i < 64; ++i) { uint32_t s0 = rr(w[i - 15], 7) ^ rr(w[i - 15], 18) ^ (w[i - 15] >> 3), s1 = rr(w[i - 2], 17) ^ rr(w[i - 2], 19) ^ (w[i - 2] >> 10); w[i] = w[i - 16] + s0 + w[i - 7] + s1; }
        uint32_t a = h[0], b = h[1], c = h[2], dd = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i) {
            uint32_t S1 = rr(e, 6) ^ rr(e, 11) ^ rr(e, 25), ch = (e & f) ^ (~e & g), t1 = hh + S1 + ch + K[i] + w[i];
            uint32_t S0 = rr(a, 2) ^ rr(a, 13) ^ rr(a, 22), mj = (a & b) ^ (a & c) ^ (b & c), t2 = S0 + mj;
            hh = g; g = f; f = e; e = dd + t1; dd = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += dd; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
    for (int i = 0; i < 8; ++i) for (int j = 0; j < 4; ++j) out[4 * i + j] = static_cast<uint8_t>(h[i] >> (24 - 8 * j));
    return true;
}
static bool sha_zly(const uint8_t*, uint64_t, uint8_t[32]) { return false; }

static void p32(std::vector<uint8_t>& v, uint32_t x) { for (int i = 0; i < 4; ++i) v.push_back(static_cast<uint8_t>(x >> (8 * i))); }
struct Dane { std::string nazwa; std::vector<uint8_t> bajty; };
static std::vector<uint8_t> zbuduj(const std::vector<Dane>& e, bool poprawny_sha = true) {
    uint64_t tabela_dl = 0; for (auto& x : e) tabela_dl += 1 + x.nazwa.size() + 12;
    std::vector<uint8_t> tab; uint64_t poz = 48 + tabela_dl;
    for (auto& x : e) { tab.push_back(static_cast<uint8_t>(x.nazwa.size())); tab.insert(tab.end(), x.nazwa.begin(), x.nazwa.end());
        p32(tab, static_cast<uint32_t>(x.bajty.size())); p32(tab, static_cast<uint32_t>(poz)); p32(tab, crc32(x.bajty.data(), x.bajty.size())); poz += x.bajty.size(); }
    std::vector<uint8_t> v{'B', 'Z', 'P', '3'}; p32(v, 1); p32(v, static_cast<uint32_t>(e.size())); p32(v, static_cast<uint32_t>(tab.size()));
    uint8_t h[32]; sha256(tab.data(), tab.size(), h); if (!poprawny_sha) h[0] ^= 1; v.insert(v.end(), h, h + 32);
    v.insert(v.end(), tab.begin(), tab.end()); for (auto& x : e) v.insert(v.end(), x.bajty.begin(), x.bajty.end());
    return v;
}
static Blad ocen(const std::vector<uint8_t>& v, uint32_t* n = nullptr) {
    static Wpis w[MAKS_WPISOW]; uint32_t liczba = 0;
    Blad b = parsuj(v.data(), v.size(), w, &liczba, &sha256);
    if (n) *n = liczba;
    return b;
}
int main(int argc, char** argv) {
    CHECK(crc32(reinterpret_cast<const uint8_t*>("123456789"), 9) == 0xCBF43926U);
    const std::vector<uint8_t> a(100, 0xAB), b(5000, 0xCD);
    auto ok = zbuduj({{"a.png", a}, {"Zdjęcie 2.JPG", b}});
    uint32_t n = 0; Wpis wp[MAKS_WPISOW];
    CHECK(ocen(ok, &n) == Blad::OK && n == 2);
    CHECK(parsuj(ok.data(), ok.size(), wp, &n, &sha256) == Blad::OK && wp[1].rozmiar == 5000 && wp[0].crc == crc32(a.data(), a.size()));
    CHECK(wp[0].przesuniecie + wp[0].rozmiar == wp[1].przesuniecie && wp[1].przesuniecie + wp[1].rozmiar == ok.size());
    // zle naglowki
    { auto v = ok; v[0] = 'X'; CHECK(ocen(v) == Blad::Sygnatura); }
    { auto v = ok; v[4] = 2; CHECK(ocen(v) == Blad::Wersja); }
    { auto v = ok; v[8] = 0; CHECK(ocen(v) == Blad::Liczba); }
    { auto v = ok; v[9] = 1; CHECK(ocen(v) == Blad::Liczba); }                  // 256+2
    { auto v = ok; v[12] = 1; CHECK(ocen(v) != Blad::OK); }                      // rozmiar tabeli
    { auto v = ok; v[16] ^= 1; CHECK(ocen(v) == Blad::SumaTabeli); }
    { auto v = ok; v[60] ^= 1; CHECK(ocen(v) == Blad::SumaTabeli); }             // tabela zmieniona bez przeliczenia SHA
    CHECK(ocen(std::vector<uint8_t>(10, 0)) == Blad::ZaMaly);
    CHECK(ocen(std::vector<uint8_t>()) == Blad::ZaMaly);
    CHECK(parsuj(ok.data(), ok.size(), wp, &n, &sha_zly) == Blad::Sha);
    CHECK(parsuj(nullptr, 100, wp, &n, &sha256) == Blad::ZaMaly);
    CHECK(parsuj(ok.data(), ok.size(), nullptr, &n, &sha256) == Blad::ZaMaly);
    // nazwy
    for (const char* zla : {"../x.png", "..", ".", "a/b.png", "a\\b.png", "x.exe", "x", ".png", "x.", "x.pn", "x.jpegg", ""})
        CHECK(ocen(zbuduj({{zla, a}})) != Blad::OK);
    { std::string n1(64, 'a'); n1 += ".png"; CHECK(ocen(zbuduj({{n1, a}})) != Blad::OK); }        // 68 B
    { std::string n1(59, 'a'); n1 += ".png"; CHECK(ocen(zbuduj({{n1, a}})) == Blad::OK); }        // 63 B
    CHECK(ocen(zbuduj({{std::string("a\x01.png"), a}})) == Blad::Nazwa);
    CHECK(ocen(zbuduj({{std::string("a\x7f.png"), a}})) == Blad::Nazwa);
    CHECK(ocen(zbuduj({{"a.png", a}, {"A.PNG", a}})) == Blad::Duplikat);
    CHECK(ocen(zbuduj({{"a.png", a}, {"a.png", b}})) == Blad::Duplikat);
    for (const char* e : {"a.png", "a.PNG", "a.jpg", "a.JPEG", "a.btp", "a.bmp"}) CHECK(ocen(zbuduj({{e, a}})) == Blad::OK);
    // rozmiary i uklad
    CHECK(ocen(zbuduj({{"a.png", std::vector<uint8_t>()}})) == Blad::Rozmiar);
    { auto v = ok; v.push_back(0); CHECK(ocen(v) == Blad::Uklad); }               // ogon po ostatnim wpisie
    { auto v = ok; v.pop_back(); CHECK(ocen(v) == Blad::Zakres); }               // wpis wychodzi poza kontener
    { auto v = ok; uint32_t off = 48 + 1 + 5 + 4;                                  // przesuniecie 1. wpisu o 1
      v[off] = static_cast<uint8_t>(v[off] + 1); // uszkodzenie tabeli => SHA
      CHECK(ocen(v) == Blad::SumaTabeli); }
    // przesuniecia ze zmiana tabeli i POPRAWNYM SHA (atak: spojny kontener z zakresami poza plikiem / nakladaniem)
    auto z_tabela = [&](auto mutator) {
        auto v = ok; uint64_t tl = (uint32_t(v[12]) | uint32_t(v[13]) << 8); std::vector<uint8_t> tab(v.begin() + 48, v.begin() + 48 + tl);
        mutator(tab); uint8_t h[32]; sha256(tab.data(), tab.size(), h); std::copy(h, h + 32, v.begin() + 16); std::copy(tab.begin(), tab.end(), v.begin() + 48); return v; };
    { auto v = z_tabela([](std::vector<uint8_t>& t) { uint32_t big = 0xFFFFFFF0U; memcpy(&t[1 + 5 + 4], &big, 4); }); CHECK(ocen(v) == Blad::Uklad); }        // przesuniecie 4 GiB
    { auto v = z_tabela([](std::vector<uint8_t>& t) { uint32_t big = 0xFFFFFFFFU; memcpy(&t[1 + 5], &big, 4); }); CHECK(ocen(v) != Blad::OK); }               // rozmiar 4 GiB
    { auto v = z_tabela([](std::vector<uint8_t>& t) { t[1 + 5 + 4] = 0; t[1 + 5 + 5] = 0; t[1 + 5 + 6] = 0; t[1 + 5 + 7] = 0; }); CHECK(ocen(v) == Blad::Uklad); }   // przesuniecie 0 (w naglowek)
    { auto v = z_tabela([](std::vector<uint8_t>& t) { t.push_back(0); }); (void)v; }
    { auto v = z_tabela([](std::vector<uint8_t>& t) { t[0] = 200; }); CHECK(ocen(v) == Blad::Tabela); }            // dlugosc nazwy poza tabela
    { auto v = z_tabela([](std::vector<uint8_t>& t) { t[0] = 0; }); CHECK(ocen(v) == Blad::Tabela); }
    // maksymalna liczba wpisow
    { std::vector<Dane> wiele; for (int i = 0; i < 256; ++i) wiele.push_back({"p" + std::to_string(i) + ".png", std::vector<uint8_t>(3, uint8_t(i))});
      uint32_t k = 0; CHECK(ocen(zbuduj(wiele), &k) == Blad::OK && k == 256); wiele.push_back({"x.png", a}); CHECK(ocen(zbuduj(wiele)) == Blad::Liczba); }
    // fuzz: dowolne uszkodzenia nie moga zawiesic ani wyjsc poza bufor (ASan/UBSan); poprawny SHA nie jest wymagany
    uint32_t rng = 0x1234567;
    auto losowe = [&]() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; };
    int przyjete = 0;
    for (int it = 0; it < 20000; ++it) {
        auto v = ok;
        int zmian = 1 + losowe() % 4;
        for (int k = 0; k < zmian; ++k) v[losowe() % std::min<size_t>(v.size(), 200)] = static_cast<uint8_t>(losowe());
        if (losowe() % 4 == 0) v.resize(losowe() % v.size());
        // naprawa SHA, zeby fuzz docieral glebiej niz sprawdzenie sumy
        if (v.size() >= 48) { uint64_t tl = uint32_t(v[12]) | uint32_t(v[13]) << 8 | uint32_t(v[14]) << 16 | uint32_t(v[15]) << 24;
            if (48 + tl <= v.size() && tl < (1u << 20)) { uint8_t h[32]; sha256(v.data() + 48, tl, h); std::copy(h, h + 32, v.begin() + 16); } }
        if (ocen(v) == Blad::OK) ++przyjete;
    }
    CHECK(przyjete < 20000);
    if (argc > 1) {                                                                  // prawdziwy modul z package_galeria.py
        FILE* f = fopen(argv[1], "rb"); CHECK(f != nullptr);
        if (f) { std::vector<uint8_t> v; uint8_t buf[65536]; size_t r; while ((r = fread(buf, 1, sizeof buf, f)) > 0) v.insert(v.end(), buf, buf + r); fclose(f);
            uint32_t k = 0; Blad bl = ocen(v, &k); CHECK(bl == Blad::OK && k >= 1);
            if (bl == Blad::OK) { Wpis w2[MAKS_WPISOW]; uint32_t k2; parsuj(v.data(), v.size(), w2, &k2, &sha256);
                for (uint32_t i = 0; i < k2; ++i) CHECK(crc32(v.data() + w2[i].przesuniecie, w2[i].rozmiar) == w2[i].crc); } }
    }
    std::printf("%s test_galeria_pak_host: %d checks, %d failed\n", g_fail ? "FAIL" : "PASS", g_ok + g_fail, g_fail);
    return g_fail ? 1 : 0;
}
