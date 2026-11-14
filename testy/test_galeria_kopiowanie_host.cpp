// Testy hosta: kopiowanie zdjec (jadro/fs/galeria_kopiowanie.h) na atrapie systemu plikow:
// brak nadpisywania plikow uzytkownika, brak miejsca, przerwane kopiowanie, blad zmiany nazwy, CRC, wyscig.
#include "../jadro/fs/galeria_kopiowanie.h"
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>
using namespace GaleriaPak;
static int g_ok = 0, g_fail = 0;
#define CHECK(c) do { if (c) ++g_ok; else { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

struct Fs {
    std::map<std::string, std::vector<uint8_t>> pliki;
    uint64_t pojemnosc = ~0ULL;
    int blad_rename = 0;                 // 1 = nastepna zmiana nazwy zawiedzie
    std::string wstaw_przed_rename;      // jesli niepuste: przed rename pojawia sie plik uzytkownika o tej nazwie
    uint64_t uzyte() const { uint64_t s = 0; for (auto& p : pliki) s += p.second.size(); return s; }
};
static Fs* F;
static Io io() {
    Io i;
    i.rozmiar = [](const char* p) -> uint32_t { auto it = F->pliki.find(p); return it == F->pliki.end() ? 0 : uint32_t(it->second.size()); };
    i.utworz = [](const char* p) { if (F->pliki.count(p)) return false; F->pliki[p] = {}; return true; };
    i.usun = [](const char* p) { return F->pliki.erase(p) > 0; };
    i.zapisz = [](const char* p, uint32_t off, const uint8_t* d, uint32_t n) {
        auto it = F->pliki.find(p); if (it == F->pliki.end() || off != it->second.size()) return false;
        if (F->uzyte() + n > F->pojemnosc) return false;
        it->second.insert(it->second.end(), d, d + n); return true; };
    i.zmien_nazwe = [](const char* p, const char* nowa) {
        if (!F->wstaw_przed_rename.empty()) { F->pliki[F->wstaw_przed_rename] = {'U', 'S', 'E', 'R'}; F->wstaw_przed_rename.clear(); }
        if (F->blad_rename) { F->blad_rename = 0; return false; }
        std::string dst = std::string(KATALOG_ZDJEC) + nowa;
        auto it = F->pliki.find(p); if (it == F->pliki.end() || F->pliki.count(dst)) return false;
        F->pliki[dst] = it->second; F->pliki.erase(it); return true; };
    i.flush = [] { return true; };
    return i;
}
struct Pak {
    std::vector<uint8_t> dane; std::vector<Wpis> wpisy;
    void dodaj(const std::string& nazwa, size_t rozmiar, uint8_t wz, bool zly_crc = false) {
        Wpis w{}; w.dl = uint8_t(nazwa.size()); memcpy(w.nazwa, nazwa.c_str(), nazwa.size());
        w.rozmiar = uint32_t(rozmiar); w.przesuniecie = uint32_t(dane.size());
        for (size_t k = 0; k < rozmiar; ++k) dane.push_back(uint8_t(wz + k));
        w.crc = crc32(dane.data() + w.przesuniecie, rozmiar) ^ (zly_crc ? 1u : 0u);
        wpisy.push_back(w);
    }
};
static std::string sc(const char* n) { return std::string(KATALOG_ZDJEC) + n; }
static bool zawartosc(const Fs& f, const std::string& s, const Pak& p, size_t idx) {
    auto it = f.pliki.find(s); if (it == f.pliki.end()) return false;
    const Wpis& w = p.wpisy[idx];
    return it->second.size() == w.rozmiar && !memcmp(it->second.data(), p.dane.data() + w.przesuniecie, w.rozmiar);
}
static bool brak_tmp(const Fs& f) { return f.pliki.count(sc(NAZWA_TMP)) == 0; }
static void pracuj(Kopiarka& k, uint32_t porcja, Krok* wynik = nullptr) { Krok r; while ((r = k.krok(porcja)) == Krok::Dalej) {} if (wynik) *wynik = r; }

int main() {
    // 1. Poprawne kopiowanie, wielokrotne porcje, brak pliku tymczasowego na koncu
    { Fs fs; F = &fs; Pak p; p.dodaj("a.jpg", 1000, 1); p.dodaj("b.png", 5, 2); p.dodaj("c.btp", 70000, 3);
      Kopiarka k; k.start(p.dane.data(), p.wpisy.data(), 3, io()); Krok r; pracuj(k, 4096, &r);
      CHECK(r == Krok::Koniec && k.zainstalowane == 3 && k.pominiete == 0 && k.bledne == 0 && k.bajty == 71005);
      for (size_t i = 0; i < 3; ++i) CHECK(zawartosc(fs, sc(p.wpisy[i].nazwa), p, i));
      CHECK(brak_tmp(fs) && fs.pliki.size() == 3); }
    // 2. Istniejacy plik uzytkownika: nie jest nadpisany ani zmieniony; reszta zainstalowana
    { Fs fs; F = &fs; Pak p; p.dodaj("a.jpg", 100, 1); p.dodaj("b.jpg", 100, 2);
      fs.pliki[sc("a.jpg")] = {'m', 'o', 'j'};
      Kopiarka k; k.start(p.dane.data(), p.wpisy.data(), 2, io()); pracuj(k, 64);
      CHECK(k.pominiete == 1 && k.zainstalowane == 1 && fs.pliki[sc("a.jpg")] == std::vector<uint8_t>({'m', 'o', 'j'}));
      CHECK(zawartosc(fs, sc("b.jpg"), p, 1) && brak_tmp(fs)); }
    // 3. Brak miejsca: niepelny plik usuniety, wczesniejsze zostaja, etap konczy sie bledem, brak pliku tymczasowego
    { Fs fs; F = &fs; fs.pojemnosc = 250; Pak p; p.dodaj("a.jpg", 100, 1); p.dodaj("b.jpg", 100, 2); p.dodaj("c.jpg", 100, 3);
      Kopiarka k; k.start(p.dane.data(), p.wpisy.data(), 3, io()); Krok r; pracuj(k, 30, &r);
      CHECK(r == Krok::KoniecBlad && k.zainstalowane == 2 && k.bledne == 1);
      CHECK(zawartosc(fs, sc("a.jpg"), p, 0) && zawartosc(fs, sc("b.jpg"), p, 1));
      CHECK(!fs.pliki.count(sc("c.jpg")) && brak_tmp(fs) && fs.uzyte() == 200);
      CHECK(k.krok(64) == Krok::Koniec); }                                   // po bledzie etap pozostaje zakonczony
    // 4. Przerwanie w polowie (utrata zasilania): pod finalna nazwa NIGDY niepelny plik; ponowny start czysci pozostalosc
    { Fs fs; F = &fs; Pak p; p.dodaj("a.jpg", 300, 1); p.dodaj("b.jpg", 300, 2);
      { Kopiarka k; k.start(p.dane.data(), p.wpisy.data(), 2, io());
        for (int i = 0; i < 5; ++i) {                                         // 5 porcji po 100 B: caly a + 2/3 b
          k.krok(100);
          for (auto& pl : fs.pliki) if (pl.first != sc(NAZWA_TMP)) CHECK(pl.second.size() == 300);   // finalne tylko kompletne
        }
        CHECK(fs.pliki.count(sc(NAZWA_TMP)) == 1); }                          // obiekt porzucony w trakcie b
      CHECK(!fs.pliki.count(sc("b.jpg")));
      Kopiarka k2; k2.start(p.dane.data(), p.wpisy.data(), 2, io()); CHECK(brak_tmp(fs));
      pracuj(k2, 100); CHECK(k2.pominiete == 1 && k2.zainstalowane == 1 && zawartosc(fs, sc("b.jpg"), p, 1) && brak_tmp(fs)); }
    // 5. CRC wpisu niezgodne: plik nie jest tworzony
    { Fs fs; F = &fs; Pak p; p.dodaj("zly.jpg", 50, 1, true); p.dodaj("ok.jpg", 50, 2);
      Kopiarka k; k.start(p.dane.data(), p.wpisy.data(), 2, io()); pracuj(k, 64);
      CHECK(k.bledne == 1 && k.zainstalowane == 1 && !fs.pliki.count(sc("zly.jpg")) && zawartosc(fs, sc("ok.jpg"), p, 1) && brak_tmp(fs)); }
    // 6. Blad zmiany nazwy: plik tymczasowy sprzatniety, kolejne wpisy kopiowane
    { Fs fs; F = &fs; Pak p; p.dodaj("a.jpg", 50, 1); p.dodaj("b.jpg", 50, 2); fs.blad_rename = 1;
      Kopiarka k; k.start(p.dane.data(), p.wpisy.data(), 2, io()); pracuj(k, 64);
      CHECK(k.bledne == 1 && k.zainstalowane == 1 && !fs.pliki.count(sc("a.jpg")) && zawartosc(fs, sc("b.jpg"), p, 1) && brak_tmp(fs)); }
    // 7. Wyscig: plik uzytkownika pojawia sie tuz przed zmiana nazwy - zostaje nietkniety
    { Fs fs; F = &fs; Pak p; p.dodaj("a.jpg", 50, 1); fs.wstaw_przed_rename = sc("a.jpg");
      Kopiarka k; k.start(p.dane.data(), p.wpisy.data(), 1, io()); pracuj(k, 64);
      CHECK(k.bledne == 1 && k.zainstalowane == 0 && fs.pliki[sc("a.jpg")] == std::vector<uint8_t>({'U', 'S', 'E', 'R'}) && brak_tmp(fs)); }   // rename nie nadpisuje
    // 8. Kolejnosc od najmniejszych (tablica kolejnosci) i pusty pakiet
    { Fs fs; F = &fs; Pak p; p.dodaj("duzy.jpg", 400, 1); p.dodaj("maly.jpg", 10, 2); uint32_t kol[2] = {1, 0};
      fs.pojemnosc = 100;
      Kopiarka k; k.start(p.dane.data(), p.wpisy.data(), 2, io(), kol); Krok r; pracuj(k, 64, &r);
      CHECK(r == Krok::KoniecBlad && zawartosc(fs, sc("maly.jpg"), p, 1) && !fs.pliki.count(sc("duzy.jpg")) && brak_tmp(fs));
      Fs fs2; F = &fs2; Kopiarka k2; k2.start(p.dane.data(), p.wpisy.data(), 0, io()); CHECK(k2.krok(64) == Krok::Koniec && fs2.pliki.empty()); }
    // 9. Stary plik tymczasowy (pozostalosc po przerwaniu) jest usuwany przy starcie
    { Fs fs; F = &fs; Pak p; p.dodaj("a.jpg", 50, 1); fs.pliki[sc(NAZWA_TMP)] = std::vector<uint8_t>(999, 7);
      Kopiarka k; k.start(p.dane.data(), p.wpisy.data(), 1, io()); CHECK(brak_tmp(fs)); pracuj(k, 64); CHECK(zawartosc(fs, sc("a.jpg"), p, 0)); }
    std::printf("%s test_galeria_kopiowanie_host: %d checks, %d failed\n", g_fail ? "FAIL" : "PASS", g_ok + g_fail, g_fail);
    return g_fail ? 1 : 0;
}
