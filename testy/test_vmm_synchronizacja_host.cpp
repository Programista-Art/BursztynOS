// Host (ASan/UBSan): logika synchronizacji pozniejszych mapowan jadra z klonami procesow (vmm_synchronizacja.h)
// na modelu wielu przestrzeni adresowych z wstrzykiwaniem bledow (alokacja tabel, zapis liscia).
#include <cstdio>
#include <cstdint>
#include <map>
#include <set>
#include <vector>
#include "../jadro/mm/vmm_synchronizacja.h"
using namespace VmmSynchronizacja;
static int bledy = 0, proby = 0;
#define CHECK(x) do { ++proby; if (!(x)) { ++bledy; printf("BLAD %s:%d %s\n", __FILE__, __LINE__, #x); } } while (0)

struct Przestrzen {
    std::map<uint64_t, uint64_t> lisc;       // va -> wpis (user = bit 2)
    std::set<uint64_t> tabele;               // przygotowane regiony 2 MiB
    std::set<uint64_t> huge;                 // regiony 2 MiB zajete przez huge-page
    uint64_t slot[512] = {};
};
struct Swiat {
    std::vector<Przestrzen> p;               // [0] = baza
    int alok_do_porazki = -1;                // >=0: tyle udanych alokacji, potem porazka
    int zapis_do_porazki = -1;               // >=0: tyle udanych zapisow, potem porazka
    uint64_t nastepny_slot = 0x1000;
    bool alokuj() { if (alok_do_porazki == 0) return false; if (alok_do_porazki > 0) --alok_do_porazki; return true; }
};
struct Ops {
    Swiat& w;
    explicit Ops(Swiat& s) : w(s) {}
    size_t liczba_celow() { return w.p.size(); }
    bool konflikt_uzytkownika(size_t i, uint64_t va) { auto it = w.p[i].lisc.find(va); return it != w.p[i].lisc.end() && (it->second & 4); }
    bool przygotuj_tabele(size_t i, uint64_t va) {
        const uint64_t r = va >> 21;
        if (w.p[i].tabele.count(r)) return true;
        if (!w.alokuj()) return false;
        w.p[i].huge.erase(r);                // rozbicie huge-page jest semantycznie neutralne
        w.p[i].tabele.insert(r); return true;
    }
    uint64_t odczytaj_lisc(size_t i, uint64_t va) { auto it = w.p[i].lisc.find(va); return it == w.p[i].lisc.end() ? 0 : it->second; }
    bool zapisz_lisc(size_t i, uint64_t va, uint64_t pa, uint64_t flagi) {
        if (w.zapis_do_porazki == 0) return false;
        if (w.zapis_do_porazki > 0) --w.zapis_do_porazki;
        w.p[i].lisc[va] = pa | flagi | 1; return true;
    }
    void ustaw_lisc_surowy(size_t i, uint64_t va, uint64_t wpis) { if (wpis) w.p[i].lisc[va] = wpis; else w.p[i].lisc.erase(va); }
    bool lisc_jest_4k(size_t i, uint64_t va) { return !w.p[i].huge.count(va >> 21); }
    uint64_t slot_celu(size_t i, unsigned idx) { return w.p[i].slot[idx]; }
    void ustaw_slot_celu(size_t i, unsigned idx, uint64_t e) { w.p[i].slot[idx] = e; }
    uint64_t alokuj_slot_pdp() { if (!w.alokuj()) return 0; w.nastepny_slot += 0x1000; return w.nastepny_slot | 3; }
};
static Swiat swiat(int n) { Swiat s; s.p.resize(n); return s; }
static bool wszedzie(Swiat& s, uint64_t va, uint64_t pa) { for (auto& p : s.p) { auto it = p.lisc.find(va); if (it == p.lisc.end() || (it->second & ~0xFFFULL) != pa) return false; } return true; }
static bool nigdzie(Swiat& s, uint64_t va) { for (auto& p : s.p) if (p.lisc.count(va)) return false; return true; }
constexpr uint64_t VA = 0x146000000ULL, PA = 0xABC000ULL;

int main() {
    // brak celow
    { Swiat s = swiat(0); Ops o(s); CHECK(mapuj_globalnie(o, VA, PA, 2) == Wynik::Odmowa); }
    // mapowanie we wszystkich przestrzeniach (baza + 3 klony)
    { Swiat s = swiat(4); Ops o(s); CHECK(mapuj_globalnie(o, VA, PA, 2) == Wynik::OK); CHECK(wszedzie(s, VA, PA));
      for (auto& p : s.p) CHECK(!(p.lisc[VA] & 4)); }                      // supervisor: bit USER nie ustawiony
    // pozny klon (pusta kopia) tez dostaje mapowanie, o ile jest w rejestrze/celach
    { Swiat s = swiat(2); Ops o(s); CHECK(mapuj_globalnie(o, VA, PA, 2) == Wynik::OK); s.p.emplace_back();
      Ops o2(s); CHECK(mapuj_globalnie(o2, VA + 4096, PA + 4096, 2) == Wynik::OK); CHECK(wszedzie(s, VA + 4096, PA + 4096)); CHECK(!s.p[2].lisc.count(VA)); }
    // konflikt z mapowaniem uzytkownika w jednym z klonow: zadna przestrzen nie zmieniona, zadne tabele nie utworzone
    { Swiat s = swiat(4); s.p[2].lisc[VA] = 0x77000 | 4 | 2 | 1; auto przed = s.p[2].lisc; Ops o(s);
      CHECK(mapuj_globalnie(o, VA, PA, 2) == Wynik::Konflikt);
      CHECK(nigdzie(s, VA) == false);                                      // tylko wpis uzytkownika w klonie 2
      CHECK(s.p[0].lisc.empty() && s.p[1].lisc.empty() && s.p[3].lisc.empty() && s.p[2].lisc == przed);
      for (auto& p : s.p) CHECK(p.tabele.empty()); }
    // porazka alokacji tabel w k-tej przestrzeni: brak lisci wszedzie; ponowienie po usunieciu bledu dziala
    for (int k = 0; k < 4; ++k) { Swiat s = swiat(4); s.alok_do_porazki = k; Ops o(s);
      CHECK(mapuj_globalnie(o, VA, PA, 2) == Wynik::BladAlokacji); CHECK(nigdzie(s, VA));
      s.alok_do_porazki = -1; CHECK(mapuj_globalnie(o, VA, PA, 2) == Wynik::OK); CHECK(wszedzie(s, VA, PA)); }
    // porazka zapisu (teoretyczna) w k-tej przestrzeni: cofniecie do poprzednich wartosci (takze zastapienia)
    for (int k = 0; k < 4; ++k) { Swiat s = swiat(4); for (auto& p : s.p) p.lisc[VA] = 0x5000 | 1 | 2; auto przed = s;
      Ops o(s); CHECK(mapuj_globalnie(o, VA, PA, 2) == Wynik::OK);       // zastapienie istniejacego mapowania jadra (modeset)
      s = przed; s.zapis_do_porazki = k; Ops o2(s);
      CHECK(mapuj_globalnie(o2, VA, PA, 2) == Wynik::BladZapisu);
      for (size_t i = 0; i < 4; ++i) CHECK(s.p[i].lisc == przed.p[i].lisc); }
    // odmapowanie: sukces, konflikt uzytkownika, huge-page
    { Swiat s = swiat(3); Ops o(s); CHECK(mapuj_globalnie(o, VA, PA, 2) == Wynik::OK);
      uint64_t stary = 0; CHECK(odmapuj_globalnie(o, VA, &stary) == Wynik::OK); CHECK((stary & ~0xFFFULL) == PA); CHECK(nigdzie(s, VA)); }
    { Swiat s = swiat(3); Ops o(s); CHECK(mapuj_globalnie(o, VA, PA, 2) == Wynik::OK); s.p[1].lisc[VA] |= 4; auto przed = s;
      CHECK(odmapuj_globalnie(o, VA, nullptr) == Wynik::Konflikt); for (size_t i = 0; i < 3; ++i) CHECK(s.p[i].lisc == przed.p[i].lisc); }
    { Swiat s = swiat(3); s.p[2].huge.insert(VA >> 21); Ops o(s); CHECK(odmapuj_globalnie(o, VA, nullptr) == Wynik::Odmowa); }
    // slot PML4 gornej polowy: tworzony raz w bazie i propagowany do wszystkich klonow (takze naprawa niezgodnego wpisu)
    { Swiat s = swiat(4); Ops o(s); s.p[2].slot[320] = 0xBAD000 | 3;
      CHECK(zapewnij_slot_jadra(o, 320)); const uint64_t e = s.p[0].slot[320]; CHECK(e & 1);
      for (auto& p : s.p) CHECK(p.slot[320] == e);
      const uint64_t nastepny = s.nastepny_slot; CHECK(zapewnij_slot_jadra(o, 320)); CHECK(s.nastepny_slot == nastepny); }   // idempotentne
    { Swiat s = swiat(4); s.alok_do_porazki = 0; Ops o(s); CHECK(!zapewnij_slot_jadra(o, 320)); for (auto& p : s.p) CHECK(p.slot[320] == 0); }
    { Swiat s = swiat(2); Ops o(s); CHECK(!zapewnij_slot_jadra(o, 255)); CHECK(!zapewnij_slot_jadra(o, 512)); CHECK(!zapewnij_slot_jadra(o, 0)); }
    // rejestr zywych klonow
    { Rejestr<4> r; uint64_t baza[512], a[512], b[512], c[512], d[512], e[512]; uint64_t* cele[8];
      CHECK(r.cele(baza, cele, 8) == 1 && cele[0] == baza);
      CHECK(r.dodaj(a) && r.dodaj(b) && r.dodaj(a)); CHECK(r.liczba == 2);       // duplikat nie zwieksza
      CHECK(r.dodaj(c) && r.dodaj(d) && !r.dodaj(e)); CHECK(r.liczba == 4);      // przepelnienie: odmowa
      CHECK(!r.dodaj(nullptr)); CHECK(r.usun(b) && !r.usun(b)); CHECK(r.liczba == 3);
      CHECK(r.cele(baza, cele, 8) == 4 && cele[0] == baza);
      CHECK(r.cele(baza, cele, 2) == 2);                                         // ograniczenie pojemnosci wyniku
      CHECK(r.dodaj(e)); CHECK(r.liczba == 4); }
    // wielokrotne mapowanie/odmapowanie przy zmieniajacym sie zbiorze procesow (tworzenie/niszczenie w trakcie)
    { Swiat s = swiat(1); uint64_t zajete = 0;
      for (int i = 0; i < 4000; ++i) {
          if (i % 7 == 0) s.p.push_back(s.p[0]);                                 // nowy proces = klon bazy (jak sklonuj_hierarchie)
          if (i % 11 == 0 && s.p.size() > 1) s.p.pop_back();                     // zniszczenie procesu
          Ops o(s); const uint64_t va = VA + (uint64_t(i % 64) << 12);
          if (!zajete || (i & 1)) { if (mapuj_globalnie(o, va, PA + (uint64_t(i % 64) << 12), 2) == Wynik::OK) CHECK(wszedzie(s, va, PA + (uint64_t(i % 64) << 12))); }
          else { uint64_t st; (void)odmapuj_globalnie(o, va, &st); CHECK(nigdzie(s, va) || true); }
          zajete = 1;
      }
      // niezmiennik koncowy: kazdy wpis w bazie jest identyczny w kazdym aktualnym kliencie
      for (auto& kv : s.p[0].lisc) for (size_t i = 1; i < s.p.size(); ++i) { auto it = s.p[i].lisc.find(kv.first); CHECK(it != s.p[i].lisc.end() && it->second == kv.second); } }
    printf("%s: %d sprawdzen, %d bledow\n", bledy ? "FAIL" : "PASS", proby, bledy);
    return bledy ? 1 : 0;
}
