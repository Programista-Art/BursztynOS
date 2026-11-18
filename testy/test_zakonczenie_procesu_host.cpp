// Host (ASan/UBSan): model tablicy procesow + odroczonego sprzatania uzywajacy PRAWDZIWEGO naglowka zakonczenie_procesu.h.
#include "../jadro/bws/zakonczenie_procesu.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
using namespace ZakonczenieProcesu;
static int bledy = 0, sprawdzenia = 0;
#define CHECK(c) do { ++sprawdzenia; if (!(c)) { ++bledy; std::printf("BLAD %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

constexpr int N = 16;
struct Zas { bool oczekuje = false; int* as = nullptr; int* stos = nullptr; };
struct Proc { int stan = STAN_PUSTY; int* as = nullptr; int* stos = nullptr; int poziom = 4; unsigned gen = 0; };
struct Model {
    Proc p[N]; Zas z[N]; int aktualny = 1; int zwolnienia = 0; int zywe = 0; int cr3 = 1;
    int utworz(int pid, int poziom = 4) {
        unsigned g = p[pid].gen; p[pid] = Proc{}; p[pid].gen = g; p[pid].stan = 1; p[pid].as = new int(1); p[pid].stos = new int(2); p[pid].poziom = poziom; ++zywe; ++p[pid].gen; return pid; }
    int zadaj(int pid) {
        int wynik = 0;
        if (pid < 0 || pid >= N) return 0;
        Proc& pr = p[pid]; Decyzja d = ocen_zadanie(pid, aktualny, pr.stan, N);
        if (d == Decyzja::Zakoncz || d == Decyzja::JuzWTrakcie) {
            if (!z[pid].oczekuje && (pr.as || pr.stos)) { z[pid] = {true, pr.as, pr.stos}; }
            if (d == Decyzja::Zakoncz) { pr.stan = STAN_KONCZACY; wynik = 1; } else wynik = 2;
        }
        return wynik; }
    void tick() {
        for (int pid = 1; pid < N; ++pid)
            if (czy_mozna_sprzatac(pid, aktualny, z[pid].oczekuje, pid == cr3) && p[pid].stan == STAN_KONCZACY) {
                delete z[pid].as; delete z[pid].stos; z[pid] = Zas{}; p[pid].as = p[pid].stos = nullptr; p[pid].stan = STAN_PUSTY; ++zwolnienia; --zywe; }
    }
    ~Model() { for (int i = 0; i < N; ++i) { if (z[i].oczekuje) { delete z[i].as; delete z[i].stos; } else if (p[i].stan != STAN_PUSTY) { delete p[i].as; delete p[i].stos; } } }
};

int main() {
    { Model m; m.utworz(2); m.utworz(3); // exactly-once + idempotencja
      CHECK(m.zadaj(2) == 1); CHECK(m.zadaj(2) == 2); CHECK(m.zadaj(2) == 2);
      m.tick(); CHECK(m.zwolnienia == 1); CHECK(m.p[2].stan == STAN_PUSTY);
      m.tick(); m.tick(); CHECK(m.zwolnienia == 1);
      CHECK(m.zadaj(2) == 0); CHECK(m.p[3].stan == 1); }
    { Model m; // odmowy
      CHECK(m.zadaj(0) == 0); CHECK(m.zadaj(-1) == 0); CHECK(m.zadaj(N) == 0); CHECK(m.zadaj(1000) == 0);
      CHECK(m.zadaj(5) == 0);                           // PUSTY
      m.p[6].stan = STAN_ZABLOKOWANY; CHECK(m.zadaj(6) == 0);   // rezerwacja loadera
      m.utworz(1); m.aktualny = 1; CHECK(m.zadaj(1) == 0);      // proces biezacy }
    }
    { Model m; m.utworz(4); m.p[4].stan = 4; // zablokowany w oczekiwaniu na zdarzenie
      CHECK(m.zadaj(4) == 1); m.tick(); CHECK(m.p[4].stan == STAN_PUSTY && m.zwolnienia == 1);
      m.utworz(5); m.p[5].stan = 5; CHECK(m.zadaj(5) == 1); m.tick(); CHECK(m.zywe == 0); }
    { Model m; m.utworz(4); m.cr3 = 4; CHECK(m.zadaj(4) == 1); m.tick(); CHECK(m.zwolnienia == 0); // aktywny CR3 nie jest sprzatany
      m.cr3 = 1; m.tick(); CHECK(m.zwolnienia == 1);
      m.utworz(7); m.aktualny = 7; m.p[7].stan = STAN_KONCZACY; m.z[7] = {true, m.p[7].as, m.p[7].stos};
      m.tick(); CHECK(m.zwolnienia == 1); m.aktualny = 1; m.tick(); CHECK(m.zwolnienia == 2); }
    { Model m; // PID reuse: nowy proces w tym samym slocie nie dziedziczy starego zadania
      m.utworz(8); unsigned g1 = m.p[8].gen; CHECK(m.zadaj(8) == 1); m.tick(); m.utworz(8);
      CHECK(m.p[8].gen != g1); CHECK(m.p[8].stan == 1); m.tick(); CHECK(m.p[8].stan == 1 && m.zwolnienia == 1);
      CHECK(m.zadaj(8) == 1); m.tick(); CHECK(m.zwolnienia == 2); }
    { Model m; // KONCZACY bez zapisanych zasobow (legacy) -> dopisanie
      m.utworz(9); m.p[9].stan = STAN_KONCZACY; CHECK(m.zadaj(9) == 2); m.tick(); CHECK(m.zwolnienia == 1 && m.zywe == 0); }
    { Model m; // petla: wiele cykli, brak wyciekow (ASan) i bilans
      for (int r = 0; r < 2000; ++r) { int pid = 2 + r % 10; m.utworz(pid); if (r % 3) m.p[pid].stan = 4; CHECK(m.zadaj(pid) == 1); CHECK(m.zadaj(pid) == 2); m.tick(); }
      CHECK(m.zywe == 0); CHECK(m.zwolnienia == 2000); }
    // autoryzacja
    CHECK(czy_wolno_zabic(2, 2, 5, 4, false)); CHECK(czy_wolno_zabic(2, 2, 5, 2, false));
    CHECK(!czy_wolno_zabic(2, 2, 5, 1, false)); CHECK(!czy_wolno_zabic(2, 2, 2, 4, false));
    CHECK(!czy_wolno_zabic(2, 2, 0, 5, false)); CHECK(!czy_wolno_zabic(2, 2, -3, 5, false));
    CHECK(!czy_wolno_zabic(2, 2, 5, 4, true));
    for (int a = 0; a < 6; ++a) for (int b = 0; b < 6; ++b) if (b < a) CHECK(!czy_wolno_zabic(3, a, 4, b, false));
    // ocen_zadanie: wszystkie stany
    for (int s = -2; s < 10; ++s) { Decyzja d = ocen_zadanie(3, 1, s, N); if (s < 0 || s > 6) CHECK(d == Decyzja::BrakProcesu); }
    CHECK(ocen_zadanie(3, 1, 1, N) == Decyzja::Zakoncz); CHECK(ocen_zadanie(3, 1, 6, N) == Decyzja::JuzWTrakcie);
    std::printf("%s: %d sprawdzen, %d bledow\n", bledy ? "FAIL" : "PASS", sprawdzenia, bledy);
    return bledy ? 1 : 0;
}
