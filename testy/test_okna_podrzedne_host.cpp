// Host (ASan/UBSan): rejestr wlasnosci okien podrzednych (jadro/bws/okna_podrzedne.h) - tworzenie, wlasnosc, duplikaty,
// limity, walidacja tresci, odbior tresci przez dziecko, sprzatanie, ponowne uzycie PID, losowa sekwencja operacji.
#include <cstdio>
#include <cstring>
#include <string>
#include <cstdint>
#include "../jadro/bws/okna_podrzedne.h"
using namespace OknaPodrzedne;
static int bledy = 0, proby = 0;
#define CHECK(x) do { ++proby; if (!(x)) { ++bledy; printf("BLAD %s:%d %s\n", __FILE__, __LINE__, #x); } } while (0)

static Wynik rez(Rejestr& r, int wl, uint32_t rodzaj, const char* tytul, const std::string& tresc, int* idx = nullptr) {
    int i = -1; Wynik w = r.zarezerwuj(wl, rodzaj, tytul, strlen(tytul), tresc.c_str(), tresc.size(), &i); if (idx) *idx = i; return w;
}
int main() {
    // tworzenie, wlasnosc, odbior tresci tylko przez zarejestrowane dziecko
    { Rejestr* r = new Rejestr(); int i = -1;
      CHECK(rez(*r, 5, RODZAJ_STEROWANIE_SNAKE, "Sterowanie", "# A\nx\ty", &i) == Wynik::OK && i >= 0);
      CHECK(!r->jest_dzieckiem(7));                                    // przed ustaw_pid nie ma jeszcze dziecka
      r->ustaw_pid(i, 7);
      CHECK(r->jest_dzieckiem(7) && r->indeks(5, RODZAJ_STEROWANIE_SNAKE) == i && r->liczba_wlasciciela(5) == 1);
      char b[128]; size_t n = r->kopiuj_dla_dziecka(7, b, sizeof(b));
      CHECK(n > 0 && std::string(b) == "Sterowanie\n\n# A\nx\ty" && b[n] == '\0');
      CHECK(r->kopiuj_dla_dziecka(8, b, sizeof(b)) == 0);              // obcy proces nie dostaje tresci
      CHECK(r->kopiuj_dla_dziecka(5, b, sizeof(b)) == 0);              // wlasciciel tez nie (BWS 102 tylko dla dziecka)
      char maly[8]; CHECK(r->kopiuj_dla_dziecka(7, maly, sizeof(maly)) == 0 && maly[0] == '\0');   // za maly bufor: nic
      CHECK(r->usun_dziecko(7) == 5 && !r->jest_dzieckiem(7) && r->liczba_wlasciciela(5) == 0 && r->usun_dziecko(7) == 0);
      delete r; }
    // duplikaty, limit na wlasciciela, limit globalny, dziecko nie otwiera okien
    { Rejestr* r = new Rejestr(); int i;
      CHECK(rez(*r, 1, 1, "A", "x", &i) == Wynik::OK); r->ustaw_pid(i, 11);
      CHECK(rez(*r, 1, 1, "A", "x") == Wynik::Duplikat);
      CHECK(rez(*r, 1, 2, "B", "x", &i) == Wynik::OK); r->ustaw_pid(i, 12);
      CHECK(rez(*r, 1, 3, "C", "x", &i) == Wynik::OK); r->ustaw_pid(i, 13);
      CHECK(rez(*r, 1, 4, "D", "x", &i) == Wynik::OK); r->ustaw_pid(i, 14);
      CHECK(rez(*r, 1, 5, "E", "x") == Wynik::LimitWlasciciela);       // 4 na wlasciciela
      CHECK(rez(*r, 11, 1, "A", "x") == Wynik::WlascicielToDziecko);   // dziecko nie jest wlascicielem
      for (int w = 2; w <= 3; ++w) for (uint32_t k = 1; k <= 2; ++k) { CHECK(rez(*r, w, k, "T", "x", &i) == Wynik::OK); r->ustaw_pid(i, 100 + w * 10 + int(k)); }
      CHECK(rez(*r, 4, 1, "T", "x") == Wynik::BrakMiejsca);            // 8 okien w systemie
      int d[8]; CHECK(r->dzieci(1, d, 8) == 4 && r->dzieci(2, d, 8) == 2 && r->dzieci(9, d, 8) == 0 && r->dzieci(1, d, 2) == 2);
      delete r; }
    // walidacja tresci i tytulu (znaki sterujace, NUL, rozmiary, rodzaje)
    { Rejestr* r = new Rejestr();
      CHECK(rez(*r, 1, 0, "A", "x") == Wynik::ZlyRodzaj && rez(*r, 1, RODZAJ_MAKS + 1, "A", "x") == Wynik::ZlyRodzaj);
      CHECK(rez(*r, 1, 1, "A", "") == Wynik::ZlaTresc);
      CHECK(rez(*r, 1, 1, "A", std::string(MAKS_TRESC + 1, 'a')) == Wynik::ZlaTresc);
      CHECK(rez(*r, 1, 1, "A", std::string(MAKS_TRESC, 'a')) == Wynik::OK);
      r->anuluj(0);
      CHECK(rez(*r, 1, 1, "A", std::string("a\0b", 3)) == Wynik::ZlaTresc);
      CHECK(rez(*r, 1, 1, "A", "a\x1b[31m") == Wynik::ZlaTresc && rez(*r, 1, 1, "A", "a\x7f") == Wynik::ZlaTresc);
      CHECK(rez(*r, 1, 1, "A", "wiersz1\nwiersz2\tkolumna ąęśćż") == Wynik::OK); r->anuluj(0);
      CHECK(rez(*r, 1, 1, "", "x") == Wynik::ZlaTresc && rez(*r, 1, 1, "T\nT", "x") == Wynik::ZlaTresc);
      CHECK(rez(*r, 1, 1, std::string(MAKS_TYTUL, 't').c_str(), "x") == Wynik::ZlaTresc);
      CHECK(rez(*r, 1, 1, std::string(MAKS_TYTUL - 1, 't').c_str(), "x") == Wynik::OK);
      CHECK(!tresc_poprawna(nullptr, 3) && !tytul_poprawny(nullptr, 3));
      delete r; }
    // zakonczenie wlasciciela: dzieci osierocone (nie do odziedziczenia po ponownym uzyciu PID), pid-y zwrocone, rezerwacje usuniete
    { Rejestr* r = new Rejestr(); int a, b, c; char buf[64];
      CHECK(rez(*r, 1, 1, "A", "x", &a) == Wynik::OK); r->ustaw_pid(a, 50);
      CHECK(rez(*r, 1, 2, "B", "x", &b) == Wynik::OK); r->ustaw_pid(b, 51);
      CHECK(rez(*r, 1, 3, "C", "x", &c) == Wynik::OK);                          // niezakonczona rezerwacja
      CHECK(rez(*r, 2, 1, "D", "x", &a) == Wynik::OK); r->ustaw_pid(a, 52);     // cudze okno nietkniete
      int d[4]; CHECK(r->osieroc(1, d, 4) == 2 && ((d[0] == 50 && d[1] == 51) || (d[0] == 51 && d[1] == 50)));
      CHECK(r->liczba_wlasciciela(1) == 0 && r->indeks(1, 1) < 0 && r->indeks(1, 3) < 0);
      CHECK(r->jest_dzieckiem(50) && r->jest_dzieckiem(51) && r->kopiuj_dla_dziecka(50, buf, sizeof(buf)) > 0);   // dziecko dostaje tresc do konca
      CHECK(rez(*r, 1, 1, "A2", "y", &a) == Wynik::OK);                         // nowy proces o PID 1 NIE dostaje starego okna jako duplikatu
      CHECK(r->liczba_wlasciciela(2) == 1 && r->usun_dziecko(50) == -1 && r->usun_dziecko(51) == -1);
      delete r; }
    // ten sam pid nie moze byc zarejestrowany dla dwoch okien; pid <= 0 i zly indeks odrzucone
    { Rejestr* r = new Rejestr(); int a, b;
      CHECK(rez(*r, 1, 1, "A", "x", &a) == Wynik::OK && r->ustaw_pid(a, 30));
      CHECK(rez(*r, 2, 1, "B", "x", &b) == Wynik::OK && !r->ustaw_pid(b, 30) && r->ustaw_pid(b, 31));
      CHECK(r->ustaw_pid(a, 30));                       // ponowne ustawienie tego samego pid na tym samym wpisie: ok
      CHECK(!r->ustaw_pid(a, 0) && !r->ustaw_pid(a, -3) && !r->ustaw_pid(-1, 40) && !r->ustaw_pid(int(MAKS_OKIEN), 40));
      delete r; }
    // anulowanie nieuruchomionej rezerwacji zwalnia miejsce; martwy wpis bez pid nie jest dzieckiem
    { Rejestr* r = new Rejestr(); int i;
      for (int k = 1; k <= 4; ++k) { CHECK(rez(*r, 1, uint32_t(k), "T", "x", &i) == Wynik::OK); }
      CHECK(rez(*r, 1, 5, "T", "x") == Wynik::LimitWlasciciela);
      r->anuluj(i); CHECK(rez(*r, 1, 5, "T", "x") == Wynik::OK);
      CHECK(r->dzieci(1, nullptr, 0) == 0);
      delete r; }
    // odswiezenie tresci istniejacego okna + ponowne uzycie PID przez NOWE okno nie dziedziczy starej tresci
    { Rejestr* r = new Rejestr(); int i; char b[256];
      CHECK(rez(*r, 2, 2, "Informacje", "stare", &i) == Wynik::OK); r->ustaw_pid(i, 21);
      CHECK(r->zaktualizuj_tresc(i, "nowe dane", 9) == Wynik::OK);
      CHECK(r->kopiuj_dla_dziecka(21, b, sizeof(b)) > 0 && std::string(b) == "Informacje\n\nnowe dane");
      CHECK(r->zaktualizuj_tresc(i, "", 0) == Wynik::ZlaTresc && r->zaktualizuj_tresc(7, "x", 1) != Wynik::OK);
      CHECK(r->usun_dziecko(21) == 2);
      CHECK(rez(*r, 3, 4, "Inne", "zupelnie inna tresc", &i) == Wynik::OK); r->ustaw_pid(i, 21);   // ten sam PID, nowy wlasciciel
      CHECK(r->kopiuj_dla_dziecka(21, b, sizeof(b)) > 0 && std::string(b) == "Inne\n\nzupelnie inna tresc" && r->liczba_wlasciciela(2) == 0);
      delete r; }
    // losowa sekwencja operacji: niezmienniki (<= MAKS_OKIEN, unikalne (wlasciciel,rodzaj) i pid-y, brak przeciekow wpisow)
    { Rejestr* r = new Rejestr(); uint64_t s = 0x9E3779B97F4A7C15ULL; auto rnd = [&] { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; };
      for (int it = 0; it < 20000; ++it) {
          const int op = int(rnd() % 4); const int wl = 1 + int(rnd() % 5); const uint32_t k = 1 + uint32_t(rnd() % 6);
          if (op == 0) { int i = -1; if (rez(*r, wl, k, "T", "tresc", &i) == Wynik::OK) { if (!(rnd() % 5) || !r->ustaw_pid(i, 200 + int(rnd() % 40))) r->anuluj(i); } }
          else if (op == 1) r->usun_dziecko(200 + int(rnd() % 40));
          else if (op == 2) { int d[8]; size_t n = r->dzieci(wl, d, 8); for (size_t q = 0; q < n; ++q) r->usun_dziecko(d[q]); }
          else { char b[200]; (void)r->kopiuj_dla_dziecka(200 + int(rnd() % 40), b, sizeof(b)); }
          size_t akt = 0; for (size_t q = 0; q < MAKS_OKIEN; ++q) if (r->w[q].aktywny) { ++akt;
              for (size_t z = q + 1; z < MAKS_OKIEN; ++z) if (r->w[z].aktywny) {
                  CHECK(!(r->w[z].wlasciciel == r->w[q].wlasciciel && r->w[z].rodzaj == r->w[q].rodzaj));
                  if (r->w[q].uruchomiony && r->w[z].uruchomiony) CHECK(r->w[q].pid_dziecka != r->w[z].pid_dziecka); } }
          CHECK(akt <= MAKS_OKIEN);
      }
      delete r; }
    // Notatnik (rodzaje 7 i 8): wlasne numery, bez kolizji z Snake/Przegladarka obrazow; duplikat jest odrzucany
    // (BWS 101 aktywuje wtedy istniejace okno); zakonczenie wlasciciela zwraca jego dzieci do zamkniecia.
    { Rejestr* r = new Rejestr(); int a = -1, b = -1, c = -1;
      CHECK(RODZAJ_O_NOTATNIKU == 7 && RODZAJ_STEROWANIE_NOTATNIK == 8 && RODZAJ_MAKS == 8);
      CHECK(RODZAJ_O_NOTATNIKU != RODZAJ_O_GRZE_SNAKE && RODZAJ_STEROWANIE_NOTATNIK != RODZAJ_STEROWANIE_SNAKE &&
            RODZAJ_STEROWANIE_NOTATNIK != RODZAJ_STEROWANIE_OBRAZY);
      CHECK(rez(*r, 30, RODZAJ_STEROWANIE_NOTATNIK, "Sterowanie", "# Plik\nCtrl+N\tnowy dokument", &a) == Wynik::OK);
      CHECK(rez(*r, 30, RODZAJ_O_NOTATNIKU, "O Notatniku", "# Notatnik dla Bursztyn OS\nŻółw ąę", &b) == Wynik::OK);
      CHECK(rez(*r, 30, RODZAJ_STEROWANIE_NOTATNIK, "Sterowanie", "x") == Wynik::Duplikat);
      CHECK(rez(*r, 31, RODZAJ_STEROWANIE_NOTATNIK, "Sterowanie", "x", &c) == Wynik::OK);   // inny Notatnik: osobne okno
      CHECK(rez(*r, 30, RODZAJ_MAKS + 1, "X", "x") == Wynik::ZlyRodzaj);
      CHECK(r->ustaw_pid(a, 40) && r->ustaw_pid(b, 41) && r->ustaw_pid(c, 42));
      CHECK(rez(*r, 40, RODZAJ_O_NOTATNIKU, "X", "x") == Wynik::WlascicielToDziecko);      // dziecko nie otwiera okien
      char t[256]; size_t n = r->kopiuj_dla_dziecka(41, t, sizeof(t));
      CHECK(n > 0 && std::string(t).find("O Notatniku\n\n# Notatnik dla Bursztyn OS") == 0);
      CHECK(r->usun_dziecko(40) == 30 && r->liczba_wlasciciela(30) == 1);                  // zamkniecie: fokus do wlasciciela 30
      CHECK(rez(*r, 30, RODZAJ_STEROWANIE_NOTATNIK, "Sterowanie", "y", &a) == Wynik::OK);   // ponowne otwarcie po zamknieciu
      CHECK(r->ustaw_pid(a, 43));
      int d[8]; const size_t m = r->dzieci(30, d, 8);
      CHECK(m == 2);
      for (size_t q = 0; q < m; ++q) CHECK(r->usun_dziecko(d[q]) == 30);
      CHECK(r->liczba_wlasciciela(30) == 0 && r->liczba_wlasciciela(31) == 1 && r->jest_dzieckiem(42));
      delete r; }
    printf("%s: %d sprawdzen, %d bledow\n", bledy ? "FAIL" : "PASS", proby, bledy);
    return bledy ? 1 : 0;
}
