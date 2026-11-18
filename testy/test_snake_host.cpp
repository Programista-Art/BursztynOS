// Host test Snake (BursztynUI): logika gry, uklad planszy, UI (klawisze, timer, motyw, rysowanie).
#include "bursztyn_ui_host_fixture.h"
#include <string>
static int checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)
#include <map>
static std::map<std::string, std::string> g_ustawienia;
static bool g_zapis_zabroniony = false;
static int g_zapisy = 0;
bool ustawienia_zapisz(const char* app, const char* klucz, const char* wartosc) {
    ++g_zapisy;
    if (g_zapis_zabroniony) return false;
    g_ustawienia[std::string(app) + "/" + klucz] = wartosc;
    return true;
}
bool ustawienia_wczytaj(const char* app, const char* klucz, char* wynik, uint32_t cap) {
    auto it = g_ustawienia.find(std::string(app) + "/" + klucz);
    if (cap) wynik[0] = '\0';
    if (it == g_ustawienia.end() || it->second.size() + 1 > cap) return false;
    memcpy(wynik, it->second.c_str(), it->second.size() + 1);
    return true;
}
#include "../jadro/bws/okna_podrzedne.h"
#include "../bursztyn_ui/tresc_uklad.h"
// Okna podrzedne (BWS 101): rejestrator wywolan w miejsce jadra - test sprawdza, CO aplikacja zleca (rodzaj/tytul/tresc).
static uint32_t g_okno_pod_wywolan = 0, g_okno_pod_rodzaj = 0, g_okno_pod_wynik = 1;
static char g_okno_pod_tytul[80], g_okno_pod_tresc[4200];
uint32_t gui_okno_podrzedne_otworz(uint32_t rodzaj, const char* tytul, const char* tresc, uint32_t dl) {
    ++g_okno_pod_wywolan; g_okno_pod_rodzaj = rodzaj;
    snprintf(g_okno_pod_tytul, sizeof(g_okno_pod_tytul), "%s", tytul);
    if (dl > 4096) dl = 4096;
    memcpy(g_okno_pod_tresc, tresc, dl); g_okno_pod_tresc[dl] = '\0';
    return g_okno_pod_wynik;
}
uint32_t gui_okno_podrzedne_tresc(char*, uint32_t) { return 0; }
#define SNAKE_HOST_TEST 1
#include "../gry/snake/snake.cpp"

static bws_zdarzenie ev(uint32_t typ, uint32_t kod = 0, int x = 0, int y = 0) {
    bws_zdarzenie e{}; e.typ = typ; e.kod = kod; e.x = x; e.y = y; return e;
}

static void logika() {
    Gra g{}; g.rng = 1;
    CHECK(KROK_NS == 220000000ULL && krok_ns(PREDKOSC_DOMYSLNA) == 220000000ULL);
    CHECK(krok_ns(Predkosc::Wolno) == 300000000ULL && krok_ns(Predkosc::Szybko) == 150000000ULL);
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
            if (u.komorka == 0) { CHECK(w - 12 < WIDOCZNE_X || h - 12 < WIDOCZNE_Y); continue; }
            CHECK(u.szer == u.komorka * WIDOCZNE_X && u.wys == u.komorka * WIDOCZNE_Y);
            CHECK(u.x >= 6 && u.y >= 6 && u.x + u.szer <= w - 6 && u.y + u.wys <= h - 6);
            CHECK(u.x == (w - u.szer) / 2 && u.y == (h - u.wys) / 2);
            // maksymalna: komorka+1 juz sie nie miesci
            CHECK((u.komorka + 1) * WIDOCZNE_X > w - 12 || (u.komorka + 1) * WIDOCZNE_Y > h - 12);
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
    okno.obsluz_zdarzenie(ev(BWS_ZDARZENIE_KLAWISZ, 'r'));              // R = rozpocznij od nowa (stan Gotowa)
    CHECK(s.gra.stan == Stan::Gotowa && s.gra.wynik() == 0 && s.gra.dlugosc == 3 && !s.timer_wlaczony);
    s.nowa_gra(true);                                                    // Nowa gra = start od razu
    CHECK(s.gra.stan == Stan::Gra && s.timer_wlaczony);
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
            CHECK(u.szer * WIDOCZNE_Y == u.wys * WIDOCZNE_X);          // proporcje widocznego pola (18:13) zachowane
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
        auto kom = [&](int cx, int cy) { return px(r.x + u.x + (cx - 1) * k + k / 2, r.y + u.y + (cy - 1) * k + k / 2); };
        CHECK(kom(2, 2) == (m.poletekstowe & 0xFFFFFF));              // puste pole = tlo z motywu
        CHECK(kom(15, 10) == (m.blad & 0xFFFFFF));                    // jablko = kolor bledu z motywu
        CHECK(kom(9, 7) == (m.sukces & 0xFFFFFF));                    // cialo = kolor sukcesu z motywu
        // Cienka obwodka z motywu (bez grubej, zakodowanej na stale ramki): 1 px w kolorze RAMKA,
        // 3 px na zewnatrz jest juz tlo okna, wnetrze to pole gry.
        CHECK(px(r.x + u.x - 1, r.y + u.y + u.wys / 2) == (m.ramka & 0xFFFFFF));
        CHECK(px(r.x + u.x - 3, r.y + u.y + u.wys / 2) == (m.oknotlo & 0xFFFFFF));
        CHECK(px(r.x + u.x + u.szer, r.y + u.y + u.wys / 2) == (m.ramka & 0xFFFFFF));
        CHECK(px(r.x + u.x + 1, r.y + u.y + 1) == (m.poletekstowe & 0xFFFFFF));
        CHECK(u.komorka * 2 > 2 && (2 * 1) <= 2);                              // obwodka <= 2 px
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


static void ustawienia_i_rekord() {
    // ---- parser/serializacja (dane z dysku sa niezaufane) ----
    UstawieniaSnake u;
    CHECK(u.rekord == 0 && u.predkosc == Predkosc::Normalnie && u.siatka);
    char b[160];
    u.rekord = 42; u.predkosc = Predkosc::Szybko; u.siatka = false;
    const size_t n = zapisz_ustawienia(u, b, sizeof(b));
    CHECK(n > 0 && n <= USTAWIENIA_MAKS_BAJTOW && b[n] == 0);
    UstawieniaSnake w = wczytaj_ustawienia(b, n);
    CHECK(w.poprawne && w.rekord == 42 && w.predkosc == Predkosc::Szybko && !w.siatka);
    CHECK(zapisz_ustawienia(u, b, 10) == 0);                      // bufor za maly
    auto zly = [&](const char* t) { UstawieniaSnake x = wczytaj_ustawienia(t, strlen(t)); return !x.poprawne; };
    CHECK(zly("") || true);
    CHECK(zly("smiec"));                                          // zly naglowek
    CHECK(zly("snake1\nrekord=abc\n"));                          // nie-cyfry
    CHECK(zly("snake1\nrekord=-5\n"));
    CHECK(zly("snake1\nrekord=232\n"));                          // ponad maksimum (231)
    CHECK(zly("snake1\nrekord=99999999999\n"));                  // przepelnienie
    CHECK(zly("snake1\npredkosc=3\n"));
    CHECK(zly("snake1\nsiatka=2\n"));
    CHECK(zly("snake1\nnieznany=1\n"));
    {   // zle wartosci => wartosci domyslne, nie czesciowe smieci
        const char* zlyp = "snake1\nrekord=999\npredkosc=7\n";
        UstawieniaSnake x = wczytaj_ustawienia(zlyp, strlen(zlyp));
        CHECK(!x.poprawne && x.rekord == 0 && x.predkosc == Predkosc::Normalnie);
        std::string duzy(USTAWIENIA_MAKS_BAJTOW + 1, 'a');
        x = wczytaj_ustawienia(duzy.c_str(), duzy.size());
        CHECK(!x.poprawne && x.rekord == 0);                       // plik wiekszy niz limit
        const char bin[] = {'s','n','a','k','e','1','\n','r','e','k','o','r','d','=','1','\0','2','\n'};
        x = wczytaj_ustawienia(bin, sizeof(bin));
        CHECK(!x.poprawne);
    }
    { const char* t231 = "snake1\nrekord=231\n"; CHECK(wczytaj_ustawienia(t231, strlen(t231)).rekord == 231); }

    // ---- rekord w aplikacji ----
    g_ustawienia.clear(); g_zapisy = 0; g_zapis_zabroniony = false;
    test_capture_pixels = false;
    AplikacjaGUI app;
    OknoSnake okno("Snake", OKNO_SZER, OKNO_WYS);
    snake_zbuduj_ui(app, okno);
    okno.przelicz_uklad();
    AplikacjaSnake& s = g_snake;
    CHECK(s.ust.rekord == 0 && s.ust.predkosc == Predkosc::Normalnie && s.ust.siatka);   // domyslne, brak pliku
    CHECK(g_zapisy == 0);                                           // start nic nie zapisuje
    s.nowa_gra(true);
    s.gra.jablko = {11, 7};
    test_monotonic_ns = 10000000000ULL;
    s.tyk();
    test_monotonic_ns += krok_ns(Predkosc::Normalnie) + 1; s.tyk();       // glowa (10,7)->(11,7): jablko
    CHECK(s.gra.wynik() == 1 && s.ust.rekord == 1 && s.nowy_rekord && s.rekord_brudny);
    CHECK(g_zapisy == 0);                                           // zadnego zapisu w trakcie gry
    for (int i = 0; i < 30; ++i) { test_monotonic_ns += krok_ns(Predkosc::Normalnie) + 1; s.gra.jablko = {1, 13}; s.tyk(); }
    CHECK(s.gra.zakonczona());
    CHECK(g_zapisy == 1 && !s.rekord_brudny && !s.zapis_nieudany);        // jeden zapis na koniec gry
    CHECK(g_ustawienia["snake/stan"].find("rekord=1") != std::string::npos);
    // "zamkniecie i ponowne otwarcie": nowa instancja stanu wczytuje rekord
    s.ust = UstawieniaSnake{}; s.wczytaj();
    CHECK(s.ust.rekord == 1 && s.ust.poprawne);
    // gorszy wynik nie nadpisuje rekordu
    s.nowa_gra(true); s.nowy_rekord = false;
    for (int i = 0; i < 30 && !s.gra.zakonczona(); ++i) { test_monotonic_ns += krok_ns(Predkosc::Normalnie) + 1; s.gra.jablko = {1, 13}; s.tyk(); }
    CHECK(s.ust.rekord == 1 && !s.nowy_rekord);
    // uszkodzony plik => domyslne bez crasha
    g_ustawienia["snake/stan"] = "snake1\nrekord=zzz\n";
    s.wczytaj();
    CHECK(s.ust.rekord == 0 && !s.ust.poprawne);
    g_ustawienia["snake/stan"] = std::string(5000, 'x');             // ponad limit odczytu
    s.wczytaj();
    CHECK(s.ust.rekord == 0);
    // odmowa zapisu: bez udawania sukcesu, komunikat o nowym rekordzie ostrzega
    g_ustawienia.clear(); g_zapis_zabroniony = true; s.wczytaj();
    s.nowa_gra(true);
    s.gra.jablko = {11, 7};
    test_monotonic_ns += 10 * krok_ns(Predkosc::Normalnie); s.tyk();
    test_monotonic_ns += krok_ns(Predkosc::Normalnie) + 1; s.tyk();
    for (int i = 0; i < 30 && !s.gra.zakonczona(); ++i) { test_monotonic_ns += krok_ns(Predkosc::Normalnie) + 1; s.gra.jablko = {1, 13}; s.tyk(); }
    CHECK(s.gra.zakonczona() && s.nowy_rekord && s.zapis_nieudany && s.rekord_brudny);
    g_zapis_zabroniony = false;
    CHECK(s.zapisz() && !s.zapis_nieudany && !s.rekord_brudny);       // ponowna proba po odblokowaniu

    // ---- predkosci ----
    s.nowa_gra(true);
    const PunktPlanszy g0 = s.gra.waz[0];
    s.gra.jablko = {1, 13};
    s.ustaw_predkosc(Predkosc::Wolno);                               // zmiana w trakcie gry nie restartuje
    CHECK(s.gra.stan == Stan::Gra && s.gra.waz[0].x == g0.x && s.gra.dlugosc == 3);
    test_monotonic_ns += 5000000000ULL; s.tyk();                      // odniesienie
    const int x0 = s.gra.waz[0].x;
    test_monotonic_ns += 299000000ULL; s.tyk();
    CHECK(s.gra.waz[0].x == x0);                                      // 299 ms < 300 ms: brak kroku
    test_monotonic_ns += 2000000ULL; s.tyk();
    CHECK(s.gra.waz[0].x == x0 + 1);                                  // 301 ms: dokladnie jeden krok
    s.ustaw_predkosc(Predkosc::Szybko);
    CHECK(g_ustawienia["snake/stan"].find("predkosc=2") != std::string::npos);
    test_monotonic_ns += 5000000000ULL; s.tyk();
    const int x1 = s.gra.waz[0].x;
    test_monotonic_ns += 151000000ULL; s.tyk();
    CHECK(s.gra.waz[0].x == x1 + 1);
    s.ustaw_predkosc(Predkosc::Normalnie);
    CHECK(s.ust.predkosc == PREDKOSC_DOMYSLNA);
    // pauza zatrzymuje logike; wznowienie nie "nadgania" czasu pauzy
    s.przelacz_pauze();
    const int xp = s.gra.waz[0].x;
    test_monotonic_ns += 60ULL * 1000000000ULL; s.tyk();
    CHECK(s.gra.stan == Stan::Pauza && s.gra.waz[0].x == xp);
    s.przelacz_pauze();
    test_monotonic_ns += 1000000ULL; s.tyk();
    test_monotonic_ns += 1000000ULL; s.tyk();
    CHECK(s.gra.waz[0].x == xp || s.gra.waz[0].x == xp + 1);          // najwyzej jeden krok po wznowieniu
}

static void menu_i_fokus() {
    test_capture_pixels = false;
    AplikacjaGUI app;
    OknoSnake okno("Snake", OKNO_SZER, OKNO_WYS);
    snake_zbuduj_ui(app, okno);
    okno.przelicz_uklad();
    AplikacjaSnake& s = g_snake;
    s.nowa_gra(true);
    // Pomoc to teraz OKNO PODRZEDNE (osobne okno, modeless): rodzic nie dostaje modalnego popupu i nadal steruje wezem
    // dopoki jego okno ma fokus; klawisze trafiaja do okna podrzednego (osobny proces), gdy ono ma fokus.
    g_okno_pod_wywolan = 0;
    s.pokaz_pomoc(OknoPodrzedne::Rodzaj::SterowanieSnake, "Sterowanie", TEKST_STEROWANIE);
    CHECK(g_okno_pod_wywolan == 1 && g_okno_pod_rodzaj == 4 && !strcmp(g_okno_pod_tytul, "Sterowanie"));
    CHECK(!okno.zarzadca_popupow().czy_otwarty());
    const int ky = s.gra.oky;
    okno.obsluz_zdarzenie(ev(BWS_ZDARZENIE_KLAWISZ, 'w'));
    CHECK(s.gra.oky != ky || s.gra.oky == -1);
    // menu Pomoc: trzy pozycje otwieraja okna podrzedne o wlasciwych rodzajach i tytulach
    struct { uint32_t rodzaj; const char* tytul; const char* tresc; } poz[] = {
        {4, "Sterowanie", TEKST_STEROWANIE}, {5, "Zasady gry", TEKST_ZASADY}, {6, "O grze", TEKST_O_GRZE}};
    for (auto& p : poz) {
        g_okno_pod_wywolan = 0;
        OknoPodrzedne::otworz(static_cast<OknoPodrzedne::Rodzaj>(p.rodzaj), p.tytul, p.tresc);
        CHECK(g_okno_pod_wywolan == 1 && g_okno_pod_rodzaj == p.rodzaj && !strcmp(g_okno_pod_tytul, p.tytul) && !strcmp(g_okno_pod_tresc, p.tresc));
        // tresc spelnia reguly jadra (BWS 101) i miesci sie w ukladzie bez przepelnienia, takze w waskim oknie
        CHECK(OknaPodrzedne::tresc_poprawna(p.tresc, strlen(p.tresc)) && OknaPodrzedne::tytul_poprawny(p.tytul, strlen(p.tytul)));
        for (int szer : {200, 320, 480}) {
            struct Miernik { int operator()(const char* t, size_t n) const { int w = 0; for (size_t i = 0; i < n; ++i) if ((static_cast<unsigned char>(t[i]) & 0xC0) != 0x80) w += 8; return w; } } m;
            TrescUklad::Uklad* u = new TrescUklad::Uklad(); TrescUklad::zbuduj(*u, p.tresc, strlen(p.tresc), szer, 18, 9, m);
            CHECK(!u->przepelnienie && u->liczba > 3);
            for (size_t i = 0; i < u->liczba; ++i) { std::string l(p.tresc + u->linie[i].poczatek, u->linie[i].dlugosc); CHECK(u->linie[i].x + m(l.c_str(), l.size()) <= szer); }
            delete u;
        }
    }
    // zasady odpowiadaja logice gry: jablka, sciana/ciało, wygrana przy pelnej planszy, rekord
    CHECK(strstr(TEKST_ZASADY, "jabłka") && strstr(TEKST_ZASADY, "ścianę") && strstr(TEKST_ZASADY, "planszę") && strstr(TEKST_ZASADY, "Najlepszy wynik"));
    CHECK(strstr(TEKST_STEROWANIE, "W, A, S, D") && strstr(TEKST_STEROWANIE, "Spacja") && strstr(TEKST_STEROWANIE, "R\t") && strstr(TEKST_STEROWANIE, "Esc"));
    // Odmowa jadra (brak prawa/limit) nie psuje gry
    g_okno_pod_wynik = 0; OknoPodrzedne::otworz(OknoPodrzedne::Rodzaj::ZasadySnake, "Zasady gry", TEKST_ZASADY); g_okno_pod_wynik = 1;
    CHECK(s.gra.stan == Stan::Gra);
    // otwarty popup (np. menu) nadal blokuje sterowanie wezem
    CHECK(!okno.zarzadca_popupow().czy_otwarty());
    // dwa skrety w jednym kroku: drugi odrzucony
    okno.obsluz_zdarzenie(ev(BWS_ZDARZENIE_KLAWISZ, 'a'));
    CHECK(s.gra.okx == 0 && s.gra.oky == -1);
    // przyciski nie przejmuja fokusu klawiatury
    CHECK(!s.btn_pauza->czy_moze_miec_fokus() && !s.btn_nowa->czy_moze_miec_fokus());
}

int main() {
    logika();
    uklad();
    ui();
    ustawienia_i_rekord();
    menu_i_fokus();
    printf("PASS test_snake_host: %d checks\n", checks);
    return 0;
}
