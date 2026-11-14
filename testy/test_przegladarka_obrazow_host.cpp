// Host test Przegladarki Obrazow (BursztynUI): parser BMP, limity, tryby, panning, cache, UI, motyw.
#include "bursztyn_ui_host_fixture.h"
#include <map>
#include <string>
#include <vector>
static int checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)

static std::map<std::string, std::vector<uint8_t>> g_pliki;
static int g_odczyty = 0;
bool czytaj_fragment_pliku(const char* sciezka, uint64_t offset, char* out, uint32_t cap, uint32_t* got) {
    ++g_odczyty;
    auto it = g_pliki.find(sciezka);
    if (it == g_pliki.end() || !out || !got) return false;
    const auto& d = it->second;
    *got = 0;
    if (offset >= d.size()) return true;
    const size_t n = std::min<size_t>(cap, d.size() - static_cast<size_t>(offset));
    memcpy(out, d.data() + offset, n);
    *got = static_cast<uint32_t>(n);
    return true;
}
static std::string g_argument;
bool pobierz_argument_startowy(char* b, uint32_t cap) {
    if (g_argument.empty() || g_argument.size() + 1 > cap) return false;
    memcpy(b, g_argument.c_str(), g_argument.size() + 1);
    return true;
}
static uint64_t hook(uint64_t nr, uint64_t a1, uint64_t a2, uint64_t, uint64_t) {
    if (nr == 4) {                                   // rozmiar pliku
        auto it = g_pliki.find(reinterpret_cast<const char*>(a1));
        if (it == g_pliki.end()) return 0;
        *reinterpret_cast<uint32_t*>(a2) = static_cast<uint32_t>(it->second.size());
        return 1;
    }
    return 0;
}

#define PRZEGLADARKA_HOST_TEST 1
#include "../programy/przegladarka-obrazow/przegladarka-obrazow.cpp"

// BMP: kolory RGB 0xRRGGBB z funkcji f(x,y); bpp 24/32; dol-gora lub gora-dol.
template <class F>
static std::vector<uint8_t> bmp(int w, int h, int bpp, bool top_down, F f) {
    const uint32_t stride = (static_cast<uint32_t>(w) * bpp / 8 + 3U) & ~3U;
    std::vector<uint8_t> d(54 + stride * h, 0);
    d[0] = 'B'; d[1] = 'M';
    auto le32 = [&](size_t o, uint32_t v) { for (int i = 0; i < 4; ++i) d[o + i] = (v >> (8 * i)) & 255; };
    le32(2, static_cast<uint32_t>(d.size())); le32(10, 54); le32(14, 40);
    le32(18, static_cast<uint32_t>(w)); le32(22, static_cast<uint32_t>(top_down ? -h : h));
    d[26] = 1; d[28] = static_cast<uint8_t>(bpp);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const uint32_t c = f(x, y);
            const int row = top_down ? y : h - 1 - y;
            uint8_t* p = &d[54 + row * stride + x * bpp / 8];
            p[0] = c & 255; p[1] = (c >> 8) & 255; p[2] = (c >> 16) & 255;
            if (bpp == 32) p[3] = 255;
        }
    return d;
}
static uint32_t cwiartki(int x, int y) { return (x < 32) ? ((y < 16) ? 0xFF0000 : 0x00FF00) : ((y < 16) ? 0x0000FF : 0xFFFF00); }

static uint32_t px(int x, int y) {
    auto it = test_pixels.find({x, y});
    return it == test_pixels.end() ? 0xDEADBEEFu : (it->second & 0xFFFFFFu);
}

static void parser() {
    uint8_t h[256] = {};
    auto dobry = bmp(8, 8, 24, false, [](int, int) { return 0x123456u; });
    memcpy(h, dobry.data(), 54);
    CHECK(parsuj_naglowek_bmp(h, 54, dobry.size()) == nullptr);
    CHECK(img_w == 8 && img_h == 8 && img_bpp == 24 && !img_top_down);
    auto td = bmp(8, 4, 32, true, [](int, int) { return 1u; });
    memcpy(h, td.data(), 54);
    CHECK(parsuj_naglowek_bmp(h, 54, td.size()) == nullptr && img_top_down && img_bpp == 32 && img_h == 4);
    // niepoprawne naglowki: kazdy musi zostac odrzucony z komunikatem
    auto zepsuj = [&](size_t off, uint32_t v, int n) {
        auto d = dobry; for (int i = 0; i < n; ++i) d[off + i] = (v >> (8 * i)) & 255;
        memcpy(h, d.data(), 54);
        return parsuj_naglowek_bmp(h, 54, d.size());
    };
    CHECK(zepsuj(0, 'X', 1) != nullptr);                 // zla sygnatura
    CHECK(zepsuj(26, 2, 2) != nullptr);                  // planes != 1
    CHECK(zepsuj(28, 8, 2) != nullptr);                  // 8 bpp nieobslugiwane
    CHECK(zepsuj(28, 16, 2) != nullptr);
    CHECK(zepsuj(30, 1, 4) != nullptr);                  // kompresja RLE
    CHECK(zepsuj(18, 0, 4) != nullptr);                  // szerokosc 0
    CHECK(zepsuj(18, 0x80000000u, 4) != nullptr);        // szerokosc ujemna
    CHECK(zepsuj(18, MAX_W + 1, 4) != nullptr);          // szerokosc ponad limit
    CHECK(zepsuj(22, 0, 4) != nullptr);                  // wysokosc 0
    CHECK(zepsuj(22, 0x80000000u, 4) != nullptr);        // INT32_MIN
    CHECK(zepsuj(22, MAX_H + 1, 4) != nullptr);
    CHECK(zepsuj(10, 10, 4) != nullptr);                 // offset danych w naglowku
    CHECK(zepsuj(10, 0x7FFFFFF0u, 4) != nullptr);        // offset poza plikiem
    CHECK(zepsuj(14, 12, 4) != nullptr);                 // DIB za maly
    memcpy(h, dobry.data(), 54);
    CHECK(parsuj_naglowek_bmp(h, 54, 60) != nullptr);    // plik ucięty (dane nie mieszcza sie)
    CHECK(parsuj_naglowek_bmp(h, 20, dobry.size()) != nullptr);
    CHECK(parsuj_naglowek_bmp(nullptr, 54, 100) != nullptr);
    // przepelnienia 64-bit nie moga przejsc
    uint64_t o = 0;
    CHECK(!mnoz_u64(UINT64_MAX, 2, &o) && !dodaj_u64(UINT64_MAX, 1, &o) && mnoz_u64(1u << 20, 1u << 20, &o));
    // limity: 8192 x 8192 x 4 = 256 MiB danych, ale plik 1 GiB maks
    CHECK(MAX_FILE_SAFE == (1ULL << 30) && MAX_W == 8192 && MAX_H == 8192);
}

static void ui() {
    zwolnij_bmp();
    test_capture_pixels = true;
    g_test_bws_hook = hook;
    AplikacjaGUI app;
    CHECK(g_obrazy.przygotuj_bufory());
    OknoObrazow okno("Przegladarka", OKNO_SZER, OKNO_WYS);
    obrazy_zbuduj_ui(app, okno);
    okno.przelicz_uklad();
    KontekstRysowania ctx;
    auto rysuj = [&] { test_pixels.clear(); okno.rysuj(ctx, true); };
    AplikacjaObrazow& s = g_obrazy;
    rysuj();
    CHECK(!img_valid);                                      // stan pusty: komunikat, bez bledu
    const Prostokat ekran0 = s.widok->prostokat_do_ekranu(Prostokat(MARGINES_OBRAZU, MARGINES_OBRAZU, s.widok->szerokosc() - 4, s.widok->wysokosc() - 4));

    // plik poprawny
    g_pliki["/a.bmp"] = bmp(64, 32, 24, false, cwiartki);
    g_pliki["/b32.bmp"] = bmp(64, 32, 32, true, cwiartki);
    CHECK(s.otworz("/a.bmp"));
    CHECK(img_valid && img_w == 64 && img_h == 32 && tryb == TrybSkalowania::DOPASUJ);
    rysuj();
    {   // Fit: obraz 64x32 w widoku ~700x340 powiekszony proporcjonalnie, wysrodkowany
        const ProstokatObrazu a = pobierz_prostokat_obrazu();
        CHECK(a.w > 400 && a.h > 200);
        const Prostokat e = s.widok->prostokat_do_ekranu(Prostokat(MARGINES_OBRAZU, MARGINES_OBRAZU, a.w, a.h));
        CHECK(px(e.x + a.w / 2 - 20, e.y + a.h / 2 - 10) == 0xFF0000);   // lewa gora czerwona
        CHECK(px(e.x + a.w / 2 - 20, e.y + a.h / 2 + 10) == 0x00FF00);
        CHECK(px(e.x + a.w / 2 + 20, e.y + a.h / 2 - 10) == 0x0000FF);
        CHECK(px(e.x + a.w / 2 + 20, e.y + a.h / 2 + 10) == 0xFFFF00);
        CHECK(px(e.x + 1, e.y + 1) == (motyw_kolor(MOTYW_KOLOR_POLETEKSTOWE) & 0xFFFFFF));  // tlo z motywu
    }
    // 1:1: obraz w oryginalnym rozmiarze, wysrodkowany; dekodowanie postepowe konczy sie
    s.jeden_do_jednego();
    CHECK(tryb == TrybSkalowania::JEDEN_DO_JEDNEGO);
    for (int i = 0; i < 200 && render_1do1.active; ++i) s.tyk();
    CHECK(!render_1do1.active && !s.timer_wlaczony);
    rysuj();
    {
        const ProstokatObrazu a = pobierz_prostokat_obrazu();
        const Prostokat e = s.widok->prostokat_do_ekranu(Prostokat(MARGINES_OBRAZU, MARGINES_OBRAZU, a.w, a.h));
        const int ox = e.x + (a.w - 64) / 2, oy = e.y + (a.h - 32) / 2;
        CHECK(px(ox + 5, oy + 5) == 0xFF0000 && px(ox + 5, oy + 25) == 0x00FF00);
        CHECK(px(ox + 60, oy + 5) == 0x0000FF && px(ox + 60, oy + 25) == 0xFFFF00);
        CHECK(px(ox - 3, oy + 5) == (motyw_kolor(MOTYW_KOLOR_POLETEKSTOWE) & 0xFFFFFF));
    }
    // bez przepelnienia widoku (obraz mniejszy od widoku): panning bez efektu
    s.przesun(100, 100);
    CHECK(view_x == 0 && view_y == 0);

    // duzy obraz: panning w granicach, 1:1, zmiana rozmiaru przycina widok
    g_pliki["/duzy.bmp"] = bmp(1600, 900, 24, false, [](int x, int y) { return static_cast<uint32_t>((x * 7) ^ (y * 13)) & 0xFFFFFF; });
    CHECK(s.otworz("/duzy.bmp"));
    s.jeden_do_jednego();
    for (int i = 0; i < 2000 && render_1do1.active; ++i) s.tyk();
    rysuj();
    ProstokatObrazu a = pobierz_prostokat_obrazu();
    const int maxx = 1600 - a.w, maxy = 900 - a.h;
    CHECK(maxx > 0 && maxy > 0);
    s.przesun(-500, -500); CHECK(view_x == 0 && view_y == 0);          // nie poza obraz
    s.przesun(100000, 100000); CHECK(view_x == maxx && view_y == maxy);
    s.przesun(-view_x, -view_y);
    s.przesun(50, 70); CHECK(view_x == 50 && view_y == 70);
    for (int i = 0; i < 2000 && render_1do1.active; ++i) s.tyk();
    rysuj();
    {   // piksel pod wskaznikiem zgadza sie z danymi zrodlowymi przesunietymi o view
        const Prostokat e = s.widok->prostokat_do_ekranu(Prostokat(MARGINES_OBRAZU, MARGINES_OBRAZU, a.w, a.h));
        CHECK(px(e.x + 10, e.y + 10) == ((((10 + 50) * 7) ^ ((10 + 70) * 13)) & 0xFFFFFF));
    }
    // zmiana rozmiaru okna: widok przycieta, stan obrazu zachowany, brak ponownego odczytu pliku dla Fit->1:1 po powrocie
    const uint32_t w0 = img_w, h0 = img_h;
    for (int w : {OKNO_SZER + 200, MIN_SZER, OKNO_SZER, 1000}) {
        for (int h : {OKNO_WYS + 150, MIN_WYS, OKNO_WYS}) {
            okno.ustaw_rozmiar(w, h); okno.przelicz_uklad();
            rysuj();
            for (int i = 0; i < 4000 && render_1do1.active; ++i) s.tyk();
            rysuj();
            CHECK(img_valid && img_w == w0 && img_h == h0 && tryb == TrybSkalowania::JEDEN_DO_JEDNEGO);
            a = pobierz_prostokat_obrazu();
            CHECK(view_x >= 0 && view_y >= 0 && view_x <= std::max(0, 1600 - a.w) && view_y <= std::max(0, 900 - a.h));
            CHECK(a.w == s.widok->szerokosc() - 4 && a.h == s.widok->wysokosc() - 4);
        }
    }
    // Fit zachowuje obraz niezaleznie od rozmiaru; powrot do Fit i cache
    s.dopasuj(); rysuj();
    CHECK(tryb == TrybSkalowania::DOPASUJ && fit_cache.valid);
    const int odczyty_przed = g_odczyty;
    rysuj(); rysuj();
    CHECK(g_odczyty == odczyty_przed);                      // cache FIT: brak ponownego dekodowania pliku
    okno.ustaw_rozmiar(OKNO_SZER + 20, OKNO_WYS + 20); okno.przelicz_uklad(); rysuj();
    CHECK(g_odczyty > odczyty_przed);                       // nowy rozmiar => przeliczenie
    // powiększ / pomniejsz (dwa poziomy skali silnika)
    s.powieksz(); CHECK(tryb == TrybSkalowania::JEDEN_DO_JEDNEGO);
    s.powieksz(); CHECK(tryb == TrybSkalowania::JEDEN_DO_JEDNEGO);
    s.pomniejsz(); CHECK(tryb == TrybSkalowania::DOPASUJ);
    s.pomniejsz(); CHECK(tryb == TrybSkalowania::DOPASUJ);

    // obsluga bledow: nieistniejacy, uszkodzony, zerowy, zbyt duzy naglowkiem; poprzedni obraz zwolniony, brak crasha
    g_pliki["/zly.bmp"] = {'B', 'M', 1, 2, 3};
    g_pliki["/zero.bmp"] = {};
    auto zbyt = bmp(8, 8, 24, false, [](int, int) { return 0u; }); zbyt[18] = zbyt[19] = zbyt[20] = zbyt[21] = 0x7F;
    g_pliki["/wielki.bmp"] = zbyt;
    for (const char* p : {"/brak.bmp", "/zly.bmp", "/zero.bmp", "/wielki.bmp", "relatywny.bmp", ""}) {
        CHECK(!s.otworz(p));
        CHECK(!img_valid && blad_txt[0] != 0);
        rysuj();                                            // rysowanie komunikatu bledu
        s.dopasuj(); s.jeden_do_jednego(); s.przesun(10, 10);   // bez obrazu: bez efektu i bez crasha
    }
    // plik 32-bit gora-dol i zamkniecie obrazu
    CHECK(s.otworz("/b32.bmp") && img_bpp == 32);
    s.zamknij_obraz();
    CHECK(!img_valid && aktualna_sciezka[0] == 0);
    // otwarcie przez argument (zdarzenie OTWORZ_PLIK)
    g_argument = "/a.bmp";
    bws_zdarzenie ev{}; ev.typ = BWS_ZDARZENIE_OTWORZ_PLIK;
    okno.obsluz_zdarzenie(ev);
    CHECK(img_valid && img_w == 64);
    g_argument.clear();

    // motyw: tlo zmienia sie, piksele obrazu - nigdy
    s.jeden_do_jednego();
    for (int i = 0; i < 200 && render_1do1.active; ++i) s.tyk();
    BursztynMotyw base = *gui_pobierz_motyw();
    BursztynMotyw temy[2] = {base, base};
    for (int v = 0; v < 2; ++v) {
        temy[v].poletekstowe = v ? 0x203040 : 0xE8E0D0;
        temy[v].oknotlo = v ? 0x101820 : 0xF0F0F0;
        test_ustaw_aktywny_motyw(&temy[v]);
        ev = bws_zdarzenie{}; ev.typ = BWS_ZDARZENIE_MOTYW_ZMIENIONY;
        okno.obsluz_zdarzenie(ev);
        okno.rysuj(ctx, true);                               // start postepowego renderu po zmianie tla
        for (int i = 0; i < 400 && render_1do1.active; ++i) s.tyk();
        rysuj();
        const ProstokatObrazu b = pobierz_prostokat_obrazu();
        const Prostokat e = s.widok->prostokat_do_ekranu(Prostokat(MARGINES_OBRAZU, MARGINES_OBRAZU, b.w, b.h));
        const int ox = e.x + (b.w - 64) / 2, oy = e.y + (b.h - 32) / 2;
        CHECK(px(ox + 5, oy + 5) == 0xFF0000 && px(ox + 60, oy + 25) == 0xFFFF00);   // kolory obrazu bez zmian
        CHECK(px(ox - 4, oy + 5) == (temy[v].poletekstowe & 0xFFFFFF));              // tlo z motywu
        CHECK(img_valid && tryb == TrybSkalowania::JEDEN_DO_JEDNEGO);
    }
    // motyw czarny: tlo 0x000000 nie moze stac sie kluczem przezroczystosci
    temy[0].poletekstowe = 0;
    test_ustaw_aktywny_motyw(&temy[0]);
    ev = bws_zdarzenie{}; ev.typ = BWS_ZDARZENIE_MOTYW_ZMIENIONY; okno.obsluz_zdarzenie(ev);
    okno.rysuj(ctx, true);
    for (int i = 0; i < 400 && render_1do1.active; ++i) s.tyk();
    rysuj();
    CHECK(kolor_tla == TECHNICZNY_CZARNY);   // nigdy 0x00000000
    // panning myszą: przeciaganie przesuwa widok, puszczenie konczy
    s.otworz("/duzy.bmp"); s.jeden_do_jednego();
    for (int i = 0; i < 2000 && render_1do1.active; ++i) s.tyk();
    rysuj();
    ZdarzenieMyszki me{}; me.typ = TypZdarzenia::WcisniecieMyszy; me.przycisk = 1; me.x = 300; me.y = 200;
    mysz_wcisnieta(s.widok, me, nullptr);
    CHECK(s.przesuwanie);
    me.typ = TypZdarzenia::RuchMyszy; me.x = 250; me.y = 180; me.modyfikatory = 1;
    mysz_ruch(s.widok, me, nullptr);
    CHECK(view_x == 50 && view_y == 20);
    me.modyfikatory = 0; me.x = 100; mysz_ruch(s.widok, me, nullptr);
    CHECK(!s.przesuwanie && view_x == 50);                // utrata przycisku konczy panning
    test_capture_pixels = false;
    s.zwolnij_bufory();
    test_ustaw_aktywny_motyw(nullptr);
}

int main() {
    parser();
    ui();
    printf("PASS test_przegladarka_obrazow_host: %d checks\n", checks);
    return 0;
}
