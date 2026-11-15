// Host: kopiuj_i_rozszerz - zmiana rozmiaru warstwy nie zostawia przezroczystej luki (ASan/UBSan).
#include <cstdio>
#include <vector>
#include <cstdint>
#include "../jadro/gui/rozszerzenie_powierzchni.h"
using namespace RozszerzeniePowierzchni;
static int bledy = 0, proby = 0;
#define CHECK(x) do { ++proby; if (!(x)) { ++bledy; printf("BLAD %s:%d %s\n", __FILE__, __LINE__, #x); } } while (0)
constexpr uint32_t CIEMNE = 0x00202428U, JASNE = 0x00ECE9E4U;

// Piksele aplikacji: RGB z gornym bajtem 0 (nie alfa 0xFF), 0 = przezroczysty.
static std::vector<uint32_t> obraz(int w, int h) {
    std::vector<uint32_t> v(size_t(w) * h);
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) v[size_t(y) * w + x] = (uint32_t(x * 3 + 1) << 16) | (uint32_t(y * 5 + 1) << 8) | 0x40;
    return v;
}
static bool wszystko_kryje(const std::vector<uint32_t>& v) { for (uint32_t p : v) if (!kryjacy(p)) return false; return true; }

int main() {
    // powiekszenie w poziomie: tresc zachowana 1:1, rozszerzenie = kolor tla motywu (jasny i ciemny), brak zer
    for (uint32_t tlo : {CIEMNE, JASNE}) {
      auto s = obraz(20, 10); std::vector<uint32_t> n(size_t(35) * 10, 0);
      CHECK(kopiuj_i_rozszerz(s.data(), 20, 10, n.data(), 35, 10, tlo));
      CHECK(wszystko_kryje(n));
      for (int y = 0; y < 10; ++y) { for (int x = 0; x < 20; ++x) CHECK(n[size_t(y) * 35 + x] == s[size_t(y) * 20 + x]);
          for (int x = 20; x < 35; ++x) CHECK(n[size_t(y) * 35 + x] == tlo); } }
    // jasny motyw nie dostaje ciemnych pasow (zadnego ciemnego koloru zapasowego)
    { auto s = obraz(8, 8); std::vector<uint32_t> n(size_t(16) * 16, 0);
      CHECK(kopiuj_i_rozszerz(s.data(), 8, 8, n.data(), 16, 16, JASNE));
      for (int y = 0; y < 16; ++y) for (int x = 0; x < 16; ++x) if (x >= 8 || y >= 8) CHECK(n[size_t(y) * 16 + x] == JASNE); }
    // powiekszenie w pionie i po przekatnej: rog tez kryje
    { auto s = obraz(12, 8); std::vector<uint32_t> n(size_t(30) * 25, 0);
      CHECK(kopiuj_i_rozszerz(s.data(), 12, 8, n.data(), 30, 25, CIEMNE)); CHECK(wszystko_kryje(n));
      CHECK(n[size_t(24) * 30 + 29] == CIEMNE); CHECK(n[0] == s[0]); }
    // zmniejszenie: przyciecie do lewego gornego rogu, bez dostepu poza stary bufor
    { auto s = obraz(40, 30); std::vector<uint32_t> n(size_t(10) * 7, 0);
      CHECK(kopiuj_i_rozszerz(s.data(), 40, 30, n.data(), 10, 7, CIEMNE));
      for (int y = 0; y < 7; ++y) for (int x = 0; x < 10; ++x) CHECK(n[size_t(y) * 10 + x] == s[size_t(y) * 40 + x]); }
    // mieszane: szerzej i nizej naraz
    { auto s = obraz(40, 30); std::vector<uint32_t> n(size_t(50) * 10, 0);
      CHECK(kopiuj_i_rozszerz(s.data(), 40, 30, n.data(), 50, 10, JASNE)); CHECK(wszystko_kryje(n)); }
    // czarny kolor motywu dostaje znacznik 0x01000000 (0 = przezroczysty)
    { std::vector<uint32_t> s(size_t(4) * 2, 0x01000000U); std::vector<uint32_t> n(size_t(7) * 2, 0);
      CHECK(kopiuj_i_rozszerz(s.data(), 4, 2, n.data(), 7, 2, 0x00000000U));
      for (int y = 0; y < 2; ++y) for (int x = 4; x < 7; ++x) CHECK(n[size_t(y) * 7 + x] == 0x01000000U);
      CHECK(piksel_tla(0xFF000000U) == 0x01000000U); CHECK(piksel_tla(0xFF123456U) == 0x00123456U); }
    // niepoprawne argumenty: nic nie zapisujemy
    { std::vector<uint32_t> n(4, 0x1234); auto s = obraz(2, 2);
      CHECK(!kopiuj_i_rozszerz(nullptr, 2, 2, n.data(), 2, 2, JASNE)); CHECK(!kopiuj_i_rozszerz(s.data(), 0, 2, n.data(), 2, 2, JASNE));
      CHECK(!kopiuj_i_rozszerz(s.data(), 2, 2, nullptr, 2, 2, JASNE)); CHECK(!kopiuj_i_rozszerz(s.data(), 2, 2, n.data(), -1, 2, JASNE));
      CHECK(n[0] == 0x1234 && n[3] == 0x1234); }
    // szybka seria zmian rozmiaru - za kazdym razem bufor w pelni kryje, a poczatkowa tresc przezywa
    { auto cur = obraz(30, 20); int w = 30, h = 20; const uint32_t p0 = cur[0], p1 = cur[size_t(5) * 30 + 7];
      const int kroki[8][2] = {{50,20},{70,35},{40,35},{40,60},{25,25},{90,25},{90,70},{30,22}};
      for (auto& k : kroki) { std::vector<uint32_t> n(size_t(k[0]) * k[1], 0);
          CHECK(kopiuj_i_rozszerz(cur.data(), w, h, n.data(), k[0], k[1], JASNE)); CHECK(wszystko_kryje(n)); cur.swap(n); w = k[0]; h = k[1]; }
      CHECK(cur[0] == p0); CHECK(cur[size_t(5) * w + 7] == p1); }
    printf("%s: %d sprawdzen, %d bledow\n", bledy ? "FAIL" : "PASS", proby, bledy);
    return bledy ? 1 : 0;
}
