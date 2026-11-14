// Host test Snake (BursztynUI): logika gry, uklad planszy, UI (klawisze, timer, motyw, rysowanie).
#include "bursztyn_ui_host_fixture.h"
#include <string>
static int checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)
#define SNAKE_HOST_TEST 1
#include "../gry/snake/snake.cpp"

static bws_zdarzenie ev(uint32_t typ, uint32_t kod = 0, int x = 0, int y = 0) {
    bws_zdarzenie e{}; e.typ = typ; e.kod = kod; e.x = x; e.y = y; return e;
}

static void logika() {
    Gra g{}; g.rng = 1;
    // inicjalizacja
    g.nowa_gra(12345, false);
    CHECK(g.stan == Stan::Gotowa && g.dlugosc == 3 && g.wynik() == 0);
    CHECK(g.waz[0].x == 10 && g.waz[0].y == 7 && g.waz[2].x == 8);
    CHECK(!g.punkt_na_wezu(g.jablko.x, g.jablko.y));
    CHECK(g.jablko.x >= 1 && g.jablko.x <= 18 && g.jablko.y >= 1 && g.jablko.y <= 13);
    g.krok();                                   // w Gotowa nic sie nie rusza
    CHECK(g.waz[0].x == 10);
    // pierwszy skret startuje gre; zawracanie o 180 stopni niedozwolone
    CHECK(!g.ustaw_kierunek(-1, 0));
    CHECK(g.stan == Stan::Gotowa);
    CHECK(g.ustaw_kierunek(0, -1) && g.stan == Stan::Gra);
    // jeden skret na krok
    CHECK(!g.ustaw_kierunek(1, 0));
    g.jablko = {1, 1};
    g.krok();
    CHECK(g.waz[0].x == 10 && g.waz[0].y == 6 && g.ky == -1);
    CHECK(!g.ustaw_kierunek(0, 1));             // 180 stopni wzgledem biezacego
    CHECK(g.ustaw_kierunek(1, 0));
    g.krok();
    CHECK(g.waz[0].x == 11 && g.waz[0].y == 6);
    CHECK(!g.ustaw_kierunek(1, 1) && !g.ustaw_kierunek(0, 0));
    // wzrost: jablko na drodze, nowy segment w miejscu starego ogona
    g.jablko = {12, 6};
    const PunktPlanszy ogon = g.waz[g.dlugosc - 1];
    g.krok();
    CHECK(g.dlugosc == 4 && g.wynik() == 1 && g.waz[3].x == ogon.x && g.waz[3].y == ogon.y);
    CHECK(!g.punkt_na_wezu(g.jablko.x, g.jablko.y));
    // pauza: krok nic nie robi, wznowienie dziala
    const PunktPlanszy glowa = g.waz[0];
    CHECK(g.przelacz_pauze() && g.stan == Stan::Pauza);
    g.krok(); g.krok();
    CHECK(g.waz[0].x == glowa.x && g.waz[0].y == glowa.y);
    CHECK(!g.ustaw_kierunek(0, 1));             // w pauzie sterowanie ignorowane
    CHECK(g.przelacz_pauze() && g.stan == Stan::Gra);
    // kolizja ze sciana
    g.nowa_gra(7, true);
    for (int i = 0; i < 20 && g.stan == Stan::Gra; ++i) { g.jablko = {1, 1}; g.krok(); }
    CHECK(g.stan == Stan::KoniecGry && g.zakonczona());
    CHECK(!g.przelacz_pauze());                 // po koncu gry pauza nie dziala
    // restart
    g.nowa_gra(9, true);
    CHECK(g.stan == Stan::Gra && g.dlugosc == 3 && g.wynik() == 0 && g.kx == 1);
    // kolizja z soba: waz dlugosci 5 zawija sie
    g.nowa_gra(3, true);
    g.dlugosc = 6;
    PunktPlanszy ciag[6] = {{8, 7}, {9, 7}, {9, 6}, {8, 6}, {7, 6}, {7, 7}};   // glowa (8,7) w petli
    for (int i = 0; i < 6; ++i) g.waz[i] = ciag[i];
    g.kx = -1; g.ky = 0; g.okx = -1; g.oky = 0; g.jablko = {1, 1};
    CHECK(g.ustaw_kierunek(0, -1));             // w gore -> (8,6) = segment ciala
    g.krok();
    CHECK(g.stan == Stan::KoniecGry);
    // jablko nigdy na wezu (calosc planszy bez jednego pola)
    g.nowa_gra(1, true);
    g.dlugosc = MAKS_DLUGOSC - 1;
    int n = 0;
    for (int y = 1; y < PLANSZA_Y - 1 && n < g.dlugosc; ++y)
        for (int x = 1; x < PLANSZA_X - 1 && n < g.dlugosc; ++x) g.waz[n++] = {x, y};
    for (int t = 0; t < 200; ++t) {
        g.generuj_jablko();
        CHECK(g.stan == Stan::Gra && !g.punkt_na_wezu(g.jablko.x, g.jablko.y));
    }
    // zwyciestwo: waz zajmuje wszystko
    g.dlugosc = MAKS_DLUGOSC;
    g.generuj_jablko();
    CHECK(g.stan == Stan::Wygrana && g.zakonczona());
    g.krok();
    CHECK(g.stan == Stan::Wygrana);
}

static void uklad() {
    // kwadratowe komorki, proporcje 4:3, wysrodkowanie, deterministyczne
    for (int w : {200, 320, 480, 640, 1000, 1900})
        for (int h : {150, 240, 360, 480, 700, 1000}) {
            const UkladPlanszy u = uklad_planszy(w, h, 6);
            if (u.komorka == 0) { CHECK(w - 12 < PLANSZA_X || h - 12 < PLANSZA_Y); continue; }
            CHECK(u.szer == u.komorka * PLANSZA_X && u.wys == u.komorka * PLANSZA_Y);
            CHECK(u.x >= 6 && u.y >= 6 && u.x + u.szer <= w - 6 && u.y + u.wys <= h - 6);
            CHECK(u.x == (w - u.szer) / 2 && u.y == (h - u.wys) / 2);
            // maksymalna: komorka+1 juz sie nie miesci
            CHECK((u.komorka + 1) * PLANSZA_X > w - 12 || (u.komorka + 1) * PLANSZA_Y > h - 12);
        }
    CHECK(uklad_planszy(0, 0, 6).komorka == 0 && uklad_planszy(-5, 100, 6).komorka == 0);
    CHECK(uklad_planszy(INT32_MAX, INT32_MAX, 6).komorka > 0);       // brak przepelnien
    CHECK(uklad_planszy(INT32_MIN, INT32_MIN, 6).komorka == 0);
    CHECK(uklad_planszy(400, 300, -9).komorka == uklad_planszy(400, 300, 0).komorka);
}

static uint32_t px(int x, int y) {
    auto it = test_pixels.find({x, y});
    return it == test_pixels.end() ? 0xDEADBEEFu : (it->second & 0xFFFFFFu);
}

static void ui() {
    test_capture_pixels = true;
    AplikacjaGUI app;
    OknoSnake okno("Snake", OKNO_SZER, OKNO_WYS);
    snake_zbuduj_ui(app, okno);
    okno.przelicz_uklad();
    KontekstRysowania ctx;
    auto rysuj = [&] { test_pixels.clear(); okno.rysuj(ctx, true); };
    AplikacjaSnake& s = g_snake;
    CHECK(s.gra.stan == Stan::Gotowa && s.plansza && s.pasek && s.btn_pauza);
    rysuj();
    // klawisze: strzalka (fizyczna) i WASD startuja gre; timer wlaczony tylko w Gra
    CHECK(!s.timer_wlaczony);
    okno.obsluz_zdarzenie(ev(BWS_ZDARZENIE_KLAWISZ, 'w'));
    CHECK(s.gra.stan == Stan::Gra && s.timer_wlaczony);
    // timer: kroki wg zegara, nie wg liczby wywolan
    test_monotonic_ns = 5000000000ULL;
    s.tyk();                                       // pierwszy tyk ustawia odniesienie
    const int y0 = s.gra.waz[0].y;
    for (int i = 0; i < 100; ++i) s.tyk();         // brak uplywu czasu => brak ruchu
    CHECK(s.gra.waz[0].y == y0);
    test_monotonic_ns += KROK_NS + 1;
    s.tyk();
    CHECK(s.gra.waz[0].y == y0 - 1);
    test_monotonic_ns += 10 * KROK_NS;             // dluga przerwa: najwyzej 3 kroki
    s.gra.jablko = {1, 1};
    s.tyk();
    CHECK(s.gra.waz[0].y >= y0 - 1 - 3 || s.gra.stan == Stan::KoniecGry);
    // nowa gra przez klawisz R i przycisk
    okno.obsluz_zdarzenie(ev(BWS_ZDARZENIE_KLAWISZ, 'r'));
    CHECK(s.gra.stan == Stan::Gra && s.gra.wynik() == 0 && s.gra.dlugosc == 3);
    // pauza: Spacja; stan nie rusza sie mimo uplywu czasu; timer wylaczony
    okno.obsluz_zdarzenie(ev(BWS_ZDARZENIE_KLAWISZ, ' '));
    CHECK(s.gra.stan == Stan::Pauza && !s.timer_wlaczony);
    const PunktPlanszy g0 = s.gra.waz[0];
    test_monotonic_ns += 100 * KROK_NS;
    s.tyk();
    CHECK(s.gra.waz[0].x == g0.x && s.gra.waz[0].y == g0.y);
    okno.obsluz_zdarzenie(ev(BWS_ZDARZENIE_KLAWISZ, ' '));
    CHECK(s.gra.stan == Stan::Gra && s.timer_wlaczony);
    // strzalki (zdarzenie fizyczne, kod USB 82 = gora)
    {
        auto e = ev(BWS_ZDARZENIE_KLAWISZ_FIZYCZNY, BWS_KEY_SOURCE_USB | 82u, 1);
        okno.obsluz_zdarzenie(e);
        CHECK(s.gra.oky == -1 && s.gra.okx == 0);
    }
    // zmiana rozmiaru okna nie zmienia stanu gry
    const Gra przed = s.gra;
    for (int w : {OKNO_SZER + 160, MIN_SZER, OKNO_SZER, 900}) {
        for (int h : {OKNO_WYS + 120, MIN_WYS, OKNO_WYS, 700}) {
            okno.ustaw_rozmiar(w, h);
            okno.przelicz_uklad();
            rysuj();
            CHECK(s.gra.dlugosc == przed.dlugosc && s.gra.jablko.x == przed.jablko.x && s.gra.stan == przed.stan);
            for (int i = 0; i < przed.dlugosc; ++i) CHECK(s.gra.waz[i].x == przed.waz[i].x && s.gra.waz[i].y == przed.waz[i].y);
            const UkladPlanszy u = uklad_planszy(s.plansza->szerokosc(), s.plansza->wysokosc(), 6);
            CHECK(u.komorka > 0);
            CHECK(u.szer * PLANSZA_Y == u.wys * PLANSZA_X);          // proporcje 4:3 zachowane
        }
    }
    // rysowanie i motyw: dwa rozne motywy => kolory planszy, weza i jablka wynikaja z motywu
    okno.ustaw_rozmiar(OKNO_SZER, OKNO_WYS); okno.przelicz_uklad();
    s.gra.nowa_gra(77, true); s.gra.waz[0] = {10, 7}; s.gra.waz[1] = {9, 7}; s.gra.waz[2] = {8, 7};
    s.gra.jablko = {15, 10}; s.po_zmianie_stanu();
    auto base = *gui_pobierz_motyw();
    BursztynMotyw temy[2] = {base, base};
    for (int wariant = 0; wariant < 2; ++wariant) {
        auto& m = temy[wariant];
        m.poletekstowe = wariant ? 0x101820 : 0xF0F0E0;
        m.sukces = wariant ? 0x33DD55 : 0x118833;
        m.blad = wariant ? 0xFF5555 : 0xCC1111;
        m.tekst = wariant ? 0xEEEEEE : 0x111111;
        m.oknotlo = wariant ? 0x202830 : 0xE0E0D0;
        test_ustaw_aktywny_motyw(&m);
        okno.obsluz_zdarzenie(ev(BWS_ZDARZENIE_MOTYW_ZMIENIONY));   // bez restartu gry
        CHECK(s.gra.stan == Stan::Gra && s.gra.dlugosc == 3);
        okno.rysuj(ctx, true);
        rysuj();
        const Prostokat r = s.plansza->prostokat_do_ekranu(Prostokat(0, 0, s.plansza->szerokosc(), s.plansza->wysokosc()));
        const UkladPlanszy u = uklad_planszy(r.szerokosc, r.wysokosc, 6);
        const int k = u.komorka;
        auto kom = [&](int cx, int cy) { return px(r.x + u.x + cx * k + k / 2, r.y + u.y + cy * k + k / 2); };
        CHECK(kom(2, 2) == (m.poletekstowe & 0xFFFFFF));              // puste pole = tlo z motywu
        CHECK(kom(15, 10) == (m.blad & 0xFFFFFF));                    // jablko = kolor bledu z motywu
        CHECK(kom(9, 7) == (m.sukces & 0xFFFFFF));                    // cialo = kolor sukcesu z motywu
        CHECK(px(r.x + u.x + 1, r.y + u.y + 1) != (m.poletekstowe & 0xFFFFFF));   // sciana inna niz tlo
        // okno: tlo i pasek z motywu
        CHECK(px(okno.x() + 2, okno.y() + okno.wysokosc() - 40) != 0xDEADBEEFu);
        CHECK(s.gra.waz[0].x == 10 && s.gra.jablko.x == 15);          // stan gry nietkniety
    }
    test_ustaw_aktywny_motyw(&temy[1]);
    // nakladki: koniec gry (wszystkie rysowania w granicach widoku)
    s.gra.stan = Stan::KoniecGry; s.po_zmianie_stanu(); rysuj();
    CHECK(!test_pixels.empty());
    for (auto& kv : test_pixels) CHECK(kv.first.first >= okno.x() - 1 && kv.first.first <= okno.x() + okno.szerokosc() + 1);
    CHECK(s.pokazany_stan == Stan::KoniecGry);
    // przycisk pauzy po koncu gry = "Jeszcze raz" => nowa gra
    s.przelacz_pauze();
    CHECK(s.gra.stan == Stan::Gra && s.gra.wynik() == 0);
    // minimalizacja wstrzymuje gre
    okno.minimalizuj();
    test_monotonic_ns += 3 * KROK_NS; s.tyk();
    CHECK(s.gra.stan == Stan::Pauza);
    test_capture_pixels = false;
    test_ustaw_aktywny_motyw(nullptr);
}

int main() {
    logika();
    uklad();
    ui();
    printf("PASS test_snake_host: %d checks\n", checks);
    return 0;
}
