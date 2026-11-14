#pragma once
/*
 * Snake - czysta logika gry (bez GUI, bez BWS), wspolna dla aplikacji Ring 3 i testow hosta.
 *
 * Plansza logiczna 20x15 ze scianami na krawedziach (pole gracza 18x13 = 234 pola).
 * Stan gry NIE zalezy od rozmiaru okna: zmiana rozmiaru zmienia tylko rysowanie.
 * Predkosc jest niezalezna od liczby klatek: aplikacja wola krok() co KROK_NS.
 */
#include <stdint.h>

namespace SnakeLogika {

constexpr int PLANSZA_X = 20;
constexpr int PLANSZA_Y = 15;
constexpr int POLA_GRACZA_X = PLANSZA_X - 2;
constexpr int POLA_GRACZA_Y = PLANSZA_Y - 2;
constexpr int MAKS_DLUGOSC = POLA_GRACZA_X * POLA_GRACZA_Y;
constexpr int DLUGOSC_START = 3;
// Predkosc gry: odstep miedzy krokami logiki (zegar monotoniczny, nie zalezy od klatek).
enum class Predkosc : uint8_t { Wolno = 0, Normalnie = 1, Szybko = 2 };
constexpr int LICZBA_PREDKOSCI = 3;
constexpr uint64_t KROK_WOLNO_NS = UINT64_C(300000000);       // 300 ms
constexpr uint64_t KROK_NORMALNIE_NS = UINT64_C(220000000);   // 220 ms (domyslna)
constexpr uint64_t KROK_SZYBKO_NS = UINT64_C(150000000);      // 150 ms
constexpr Predkosc PREDKOSC_DOMYSLNA = Predkosc::Normalnie;
constexpr uint64_t krok_ns(Predkosc p) {
    return p == Predkosc::Wolno ? KROK_WOLNO_NS : (p == Predkosc::Szybko ? KROK_SZYBKO_NS : KROK_NORMALNIE_NS);
}
constexpr uint64_t KROK_NS = KROK_NORMALNIE_NS;
// Widoczne pole gry (bez logicznych scian na obwodzie siatki 20x15).
constexpr int WIDOCZNE_X = PLANSZA_X - 2;
constexpr int WIDOCZNE_Y = PLANSZA_Y - 2;

enum class Stan : uint8_t { Gotowa, Gra, Pauza, KoniecGry, Wygrana };
enum class Kierunek : uint8_t { Gora, Dol, Lewo, Prawo };

struct PunktPlanszy { int x, y; };

struct Gra {
    PunktPlanszy waz[MAKS_DLUGOSC + 1];
    PunktPlanszy jablko;
    int dlugosc;
    int kx, ky;               // kierunek biezacy (uzyty w ostatnim kroku)
    int okx, oky;             // kierunek oczekujacy (jeden skret na krok)
    bool skret_w_kroku;
    Stan stan;
    uint64_t rng;

    // ---- inicjalizacja -------------------------------------------------
    void nowa_gra(uint64_t ziarno, bool od_razu_gra) {
        dlugosc = DLUGOSC_START;
        waz[0] = {PLANSZA_X / 2, PLANSZA_Y / 2};
        waz[1] = {PLANSZA_X / 2 - 1, PLANSZA_Y / 2};
        waz[2] = {PLANSZA_X / 2 - 2, PLANSZA_Y / 2};
        kx = okx = 1; ky = oky = 0;
        skret_w_kroku = false;
        rng ^= ziarno;
        if (rng == 0) rng = UINT64_C(0xA0761D6478BD642F);
        stan = od_razu_gra ? Stan::Gra : Stan::Gotowa;
        generuj_jablko();
    }

    int wynik() const { return dlugosc - DLUGOSC_START; }
    bool aktywna() const { return stan == Stan::Gra; }
    bool zakonczona() const { return stan == Stan::KoniecGry || stan == Stan::Wygrana; }

    // ---- sterowanie ----------------------------------------------------
    // Zwraca true, gdy kierunek zostal przyjety. Gotowa -> Gra przy pierwszym poprawnym skrecie.
    bool ustaw_kierunek(int nx, int ny) {
        if (stan != Stan::Gra && stan != Stan::Gotowa) return false;
        if ((nx == 0 && ny == 0) || (nx != 0 && ny != 0)) return false;
        if (stan == Stan::Gra && skret_w_kroku) return false;
        if (nx == -kx && ny == -ky) return false;       // brak zawracania o 180 stopni
        okx = nx; oky = ny;
        skret_w_kroku = true;
        if (stan == Stan::Gotowa) stan = Stan::Gra;
        return true;
    }
    bool ustaw_kierunek(Kierunek k) {
        switch (k) {
            case Kierunek::Gora: return ustaw_kierunek(0, -1);
            case Kierunek::Dol: return ustaw_kierunek(0, 1);
            case Kierunek::Lewo: return ustaw_kierunek(-1, 0);
            case Kierunek::Prawo: return ustaw_kierunek(1, 0);
        }
        return false;
    }

    // Pauza <-> wznowienie; z Gotowa startuje gre. Zwraca true, gdy stan sie zmienil.
    bool przelacz_pauze() {
        if (stan == Stan::Gra) { stan = Stan::Pauza; return true; }
        if (stan == Stan::Pauza || stan == Stan::Gotowa) { stan = Stan::Gra; return true; }
        return false;
    }

    // ---- krok gry (wolany przez timer, nigdy z renderu) ------------------
    void krok() {
        if (stan != Stan::Gra) return;
        kx = okx; ky = oky;
        skret_w_kroku = false;
        // Uderzenie w sciane konczy gre bez ruchu: glowa zostaje na ostatnim polu planszy.
        const int nx = waz[0].x + kx, ny = waz[0].y + ky;
        if (nx <= 0 || nx >= PLANSZA_X - 1 || ny <= 0 || ny >= PLANSZA_Y - 1) {
            stan = Stan::KoniecGry; return;
        }
        const PunktPlanszy stary_ogon = waz[dlugosc - 1];
        for (int i = dlugosc - 1; i > 0; --i) waz[i] = waz[i - 1];
        waz[0].x = nx; waz[0].y = ny;
        for (int i = 1; i < dlugosc; ++i)
            if (waz[0].x == waz[i].x && waz[0].y == waz[i].y) { stan = Stan::KoniecGry; return; }
        if (waz[0].x == jablko.x && waz[0].y == jablko.y) {
            if (dlugosc < MAKS_DLUGOSC) { waz[dlugosc] = stary_ogon; ++dlugosc; }
            generuj_jablko();
        }
    }

    // ---- jablko --------------------------------------------------------
    bool punkt_na_wezu(int x, int y) const {
        for (int i = 0; i < dlugosc; ++i) if (waz[i].x == x && waz[i].y == y) return true;
        return false;
    }

    uint32_t losuj() {
        uint64_t x = rng;                     // xorshift64*: PRNG gry, nie kryptografia
        if (x == 0) x = UINT64_C(0xD1B54A32D192ED03);
        x ^= x >> 12; x ^= x << 25; x ^= x >> 27;
        rng = x;
        return static_cast<uint32_t>((x * UINT64_C(2685821657736338717)) >> 32);
    }

    void generuj_jablko() {
        if (dlugosc >= MAKS_DLUGOSC) { stan = Stan::Wygrana; return; }
        for (int p = 0; p < MAKS_DLUGOSC * 2; ++p) {
            const int x = 1 + static_cast<int>(losuj() % POLA_GRACZA_X);
            const int y = 1 + static_cast<int>(losuj() % POLA_GRACZA_Y);
            if (!punkt_na_wezu(x, y)) { jablko = {x, y}; return; }
        }
        for (int y = 1; y < PLANSZA_Y - 1; ++y)
            for (int x = 1; x < PLANSZA_X - 1; ++x)
                if (!punkt_na_wezu(x, y)) { jablko = {x, y}; return; }
        stan = Stan::Wygrana;
    }
};

// ---- uklad planszy w widoku (czysta geometria, int64 posrednio) ---------
struct UkladPlanszy {
    int komorka;       // bok kwadratowej komorki w px (0 = widok za maly)
    int x, y;          // lewy gorny rog planszy w widoku
    int szer, wys;     // widoczne pole gry w px = komorka * 18 x 13 (logiczna siatka 20x15 ma sciany na obwodzie)
};

constexpr UkladPlanszy uklad_planszy(int widok_w, int widok_h, int margines) {
    if (margines < 0) margines = 0;
    const int64_t w = static_cast<int64_t>(widok_w) - 2LL * margines;
    const int64_t h = static_cast<int64_t>(widok_h) - 2LL * margines;
    if (w < WIDOCZNE_X || h < WIDOCZNE_Y) return {0, 0, 0, 0, 0};
    const int64_t k1 = w / WIDOCZNE_X, k2 = h / WIDOCZNE_Y;
    const int k = static_cast<int>(k1 < k2 ? k1 : k2);
    const int pw = k * WIDOCZNE_X, ph = k * WIDOCZNE_Y;
    return {k, static_cast<int>((widok_w - pw) / 2), static_cast<int>((widok_h - ph) / 2), pw, ph};
}

} // namespace SnakeLogika
