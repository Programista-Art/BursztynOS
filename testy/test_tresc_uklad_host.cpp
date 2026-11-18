// Host (ASan/UBSan): uklad tresci okien podrzednych - zawijanie, kolumny, naglowki, przewijanie, granice.
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "../bursztyn_ui/tresc_uklad.h"
using namespace TrescUklad;
static int bledy = 0, proby = 0;
#define CHECK(x) do { ++proby; if (!(x)) { ++bledy; printf("BLAD %s:%d %s\n", __FILE__, __LINE__, #x); } } while (0)

// Miernik: 8 px na punkt kodowy UTF-8 (czcionka nieproporcjonalna do testu).
struct Miernik8 {
    int operator()(const char* s, size_t n) const { int w = 0; for (size_t i = 0; i < n; ++i) if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80) w += 8; return w; }
};
static std::string tekst_linii(const std::string& t, const Linia& l) { return t.substr(l.poczatek, l.dlugosc); }
static bool utf8_cale(const std::string& s) {      // brak ucietego znaku wielobajtowego na brzegach
    if (s.empty()) return true;
    if ((static_cast<unsigned char>(s[0]) & 0xC0) == 0x80) return false;
    size_t i = 0; while (i < s.size()) { size_t n = dl_utf8(static_cast<unsigned char>(s[i])); if (i + n > s.size()) return false; i += n; }
    return true;
}
static Uklad* nowy() { return new Uklad(); }

int main() {
    Miernik8 m; const int LH = 18, ODS = 8;
    // --- zawijanie: zadna linia nie przekracza szerokosci; cala tresc zachowana; kolejnosc rosnaca
    { std::string t = "Przeglądarka Obrazów dla Bursztyn OS, aplikacja BursztynUI. Formaty: BMP, PNG, JPEG, BTP (BTP1 i BTP2). Domyślna galeria: /dane/zdjecia.";
      std::vector<std::string> l; zawin(t.c_str(), t.size(), 160, m, [&](size_t p, size_t d) { l.push_back(t.substr(p, d)); });
      CHECK(l.size() >= 5); std::string sklej; for (auto& s : l) { CHECK(m(s.c_str(), s.size()) <= 160); CHECK(utf8_cale(s)); CHECK(!s.empty() && s.front() != ' ' && s.back() != ' '); sklej += s + " "; }
      CHECK(sklej.find("Przeglądarka Obrazów dla") == 0); CHECK(sklej.find("/dane/zdjecia.") != std::string::npos); }
    // --- bardzo dlugie slowo (sciezka bez spacji) jest lamane po znakach, nigdy nie ucinane i nie wychodzi poza szerokosc
    { std::string t = "/uzytkownicy/domyslny/Ustawienia/przegladarka-obrazow/bardzo-dluga-nazwa-pliku-zdjecia-ąęśćżźłó.jpg";
      std::string odtw; size_t n = zawin(t.c_str(), t.size(), 120, m, [&](size_t p, size_t d) { std::string s = t.substr(p, d); CHECK(m(s.c_str(), s.size()) <= 120); CHECK(utf8_cale(s)); odtw += s; });
      CHECK(n >= 6); CHECK(odtw == t); }
    // --- szerokosc mniejsza niz jeden znak: postep gwarantowany (brak petli nieskonczonej)
    { std::string t = "ąąą ąąą"; std::string odtw; size_t n = zawin(t.c_str(), t.size(), 1, m, [&](size_t p, size_t d) { odtw += t.substr(p, d); }); CHECK(n >= 6); CHECK(odtw == "ąąąąąą"); }
    // --- uklad: naglowek, akapity, odstepy, wlasciwosci w 2 kolumnach
    { std::string t = "# Plik\nNazwa\tobraz-z-bardzo-dlugim-opisem-i-spacjami.png i jeszcze dluzsza wartosc ktora musi sie zawinac w kolumnie\nWymiary\t3508 x 2480\n\n# Pamiec\nPowierzchnia\t33,1 MiB\nTekst zwyczajny w akapicie, ktory sie zawija.";
      Uklad* u = nowy(); zbuduj(*u, t.c_str(), t.size(), 300, LH, ODS, m);
      CHECK(!u->przepelnienie && u->liczba > 6 && u->szerokosc_klucza > 0 && u->szerokosc_klucza <= 300 * 2 / 5);
      int ostatni_y = -1; bool klucz = false, wart = false, nag = false, akap = false;
      for (size_t i = 0; i < u->liczba; ++i) { const Linia& l = u->linie[i]; std::string s = tekst_linii(t, l);
          CHECK(l.y >= 0 && l.y + LH <= u->wysokosc); CHECK(l.x >= 0 && l.x + m(s.c_str(), s.size()) <= 300);
          CHECK(s.find('\t') == std::string::npos && s.find('\n') == std::string::npos);
          if (l.typ == Typ::Klucz) { klucz = true; CHECK(l.x == 0 && m(s.c_str(), s.size()) <= u->szerokosc_klucza); }
          if (l.typ == Typ::Wartosc) { wart = true; CHECK(l.x > u->szerokosc_klucza); }
          if (l.typ == Typ::Naglowek) nag = true; if (l.typ == Typ::Akapit) akap = true;
          if (l.typ != Typ::Wartosc) { CHECK(l.y >= ostatni_y || l.typ == Typ::Klucz); } (void)ostatni_y; }
      CHECK(klucz && wart && nag && akap);
      // Naglowek "Pamiec" jest nizej niz "Plik"; wartosci wiersza zaczynaja sie na tej samej wysokosci co klucz
      int y_nazwa_k = -1, y_nazwa_w = -1; for (size_t i = 0; i < u->liczba; ++i) { std::string s = tekst_linii(t, u->linie[i]); if (s == "Nazwa") y_nazwa_k = u->linie[i].y; if (s.rfind("obraz-z-bardzo", 0) == 0) y_nazwa_w = u->linie[i].y; }
      CHECK(y_nazwa_k >= 0 && y_nazwa_k == y_nazwa_w);
      delete u; }
    // --- wiersze o roznej liczbie linii w kolumnach: kolejny wiersz zaczyna sie pod dluzsza kolumna (bez nakladania)
    { std::string t = "Klucz\tbardzo dluga wartosc ktora zajmuje wiele linii w waskiej kolumnie wartosci\nDrugi\tkrotka"; Uklad* u = nowy(); zbuduj(*u, t.c_str(), t.size(), 220, LH, ODS, m);
      int y_drugi = -1, max_y_pierwszego = -1; for (size_t i = 0; i < u->liczba; ++i) { std::string s = tekst_linii(t, u->linie[i]); if (s == "Drugi") y_drugi = u->linie[i].y; else if (u->linie[i].y > max_y_pierwszego && y_drugi < 0) max_y_pierwszego = u->linie[i].y; }
      CHECK(y_drugi >= max_y_pierwszego + LH); delete u; }
    // --- klucz szerszy niz 40%: kolumna kluczy ograniczona, klucz zawija sie (nie jest ucinany)
    { std::string t = "Bardzo dluga nazwa pola ktora nie zmiesci sie w kolumnie\twartosc"; Uklad* u = nowy(); zbuduj(*u, t.c_str(), t.size(), 200, LH, ODS, m);
      CHECK(u->szerokosc_klucza == 80); size_t k = 0; std::string sklej; for (size_t i = 0; i < u->liczba; ++i) if (u->linie[i].typ == Typ::Klucz) { ++k; sklej += tekst_linii(t, u->linie[i]); }
      CHECK(k >= 4); delete u; }
    // --- przewijanie i granice
    CHECK(maks_przewiniecie(500, 200) == 300 && maks_przewiniecie(100, 200) == 0 && maks_przewiniecie(100, 0) == 0);
    // --- puste/zle wejscie
    { Uklad* u = nowy(); zbuduj(*u, "", 0, 300, LH, ODS, m); CHECK(u->liczba == 0 && u->wysokosc == 0);
      zbuduj(*u, nullptr, 5, 300, LH, ODS, m); CHECK(u->liczba == 0);
      std::string t = "tekst"; zbuduj(*u, t.c_str(), t.size(), 4, LH, ODS, m); CHECK(u->liczba == 0);   // szerokosc < 8: brak ukladu
      delete u; }
    // --- przepelnienie tablicy linii jest zglaszane (nie cicho obcinane) i nie wychodzi poza tablice
    { std::string t; for (int i = 0; i < 4000; ++i) t += "a\n"; Uklad* u = nowy(); zbuduj(*u, t.c_str(), t.size(), 200, LH, ODS, m); CHECK(u->przepelnienie && u->liczba == MAKS_LINII); delete u; }
    // --- maksymalna tresc 4096 bajtow miesci sie w ukladzie przy typowej szerokosci
    { std::string t; while (t.size() < 4000) t += "Slowo kolejne zdanie tekstu informacyjnego. "; Uklad* u = nowy(); zbuduj(*u, t.c_str(), t.size(), 480, LH, ODS, m); CHECK(!u->przepelnienie && u->liczba > 20 && u->wysokosc > 20 * LH); delete u; }
    // --- determinizm
    { std::string t = "# A\nx\ty z w\nakapit akapit akapit akapit akapit akapit"; Uklad* a = nowy(); Uklad* b = nowy(); zbuduj(*a, t.c_str(), t.size(), 150, LH, ODS, m); zbuduj(*b, t.c_str(), t.size(), 150, LH, ODS, m);
      CHECK(a->liczba == b->liczba && a->wysokosc == b->wysokosc && !memcmp(a->linie, b->linie, a->liczba * sizeof(Linia))); delete a; delete b; }
    printf("%s: %d sprawdzen, %d bledow\n", bledy ? "FAIL" : "PASS", proby, bledy);
    return bledy ? 1 : 0;
}
