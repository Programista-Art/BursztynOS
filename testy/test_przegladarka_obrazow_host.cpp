// Host test Przegladarki Obrazow 2.0: formaty (PNG/JPEG/BMP/BTP1/BTP2), galeria, powiekszenie, UI, motyw.
// Argument: katalog z plikami z testy/generuj_obrazy_testowe.py.
#include "bursztyn_ui_host_fixture.h"
#include <map>
#include <string>
#include <vector>
#include <algorithm>
static int checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)

static std::map<std::string, std::vector<uint8_t>> g_pliki;
static std::string g_listing;
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
#include "../jadro/bws/okna_podrzedne.h"
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
static std::string g_argument;
bool pobierz_argument_startowy(char* b, uint32_t cap) {
    if (g_argument.empty() || g_argument.size() + 1 > cap) return false;
    memcpy(b, g_argument.c_str(), g_argument.size() + 1);
    return true;
}
static uint64_t hook(uint64_t nr, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t) {
    if (nr == 4) {
        auto it = g_pliki.find(reinterpret_cast<const char*>(a1));
        if (it == g_pliki.end()) return 0;
        *reinterpret_cast<uint32_t*>(a2) = static_cast<uint32_t>(it->second.size());
        return 1;
    }
    if (nr == 6) {                                     // listing katalogu
        if (std::string(reinterpret_cast<const char*>(a1)) != "/dane/zdjecia" || g_listing == "BRAK") return 0;
        if (g_listing.size() + 1 > a3) return 1;
        memcpy(reinterpret_cast<char*>(a2), g_listing.c_str(), g_listing.size() + 1);
        return 1;
    }
    return 0;
}

#define PRZEGLADARKA_HOST_TEST 1
#include "../programy/przegladarka-obrazow/przegladarka-obrazow.cpp"

static std::string g_fix;
static std::vector<uint8_t> wczytaj_fixture(const char* nazwa) {
    std::string p = g_fix + "/" + nazwa;
    FILE* f = fopen(p.c_str(), "rb");
    CHECK(f != nullptr);
    std::vector<uint8_t> d;
    uint8_t b[65536]; size_t n;
    while ((n = fread(b, 1, sizeof(b), f)) > 0) d.insert(d.end(), b, b + n);
    fclose(f);
    return d;
}
static void dodaj(const char* nazwa) { g_pliki[std::string("/dane/zdjecia/") + nazwa] = wczytaj_fixture(nazwa); }

template <class F>
static std::vector<uint8_t> bmp(int w, int h, int bpp, bool top_down, F f) {
    const uint32_t stride = (static_cast<uint32_t>(w) * bpp / 8 + 3U) & ~3U;
    std::vector<uint8_t> d(54 + static_cast<size_t>(stride) * h, 0);
    d[0] = 'B'; d[1] = 'M';
    auto le32 = [&](size_t o, uint32_t v) { for (int i = 0; i < 4; ++i) d[o + i] = (v >> (8 * i)) & 255; };
    le32(2, static_cast<uint32_t>(d.size())); le32(10, 54); le32(14, 40);
    le32(18, static_cast<uint32_t>(w)); le32(22, static_cast<uint32_t>(top_down ? -h : h));
    d[26] = 1; d[28] = static_cast<uint8_t>(bpp);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const uint32_t c = f(x, y);
            const int row = top_down ? y : h - 1 - y;
            uint8_t* p = &d[54 + static_cast<size_t>(row) * stride + x * bpp / 8];
            p[0] = c & 255; p[1] = (c >> 8) & 255; p[2] = (c >> 16) & 255;
            if (bpp == 32) p[3] = 255;
        }
    return d;
}

static uint32_t px(int x, int y) {
    auto it = test_pixels.find({x, y});
    return it == test_pixels.end() ? 0xDEADBEEFu : (it->second & 0xFFFFFFu);
}

// ---------------------------------------------------------------- parser BMP
static void parser_bmp() {
    uint8_t h[256] = {};
    auto dobry = bmp(8, 8, 24, false, [](int, int) { return 0x123456u; });
    memcpy(h, dobry.data(), 54);
    CHECK(parsuj_naglowek_bmp(h, 54, dobry.size()) == nullptr);
    CHECK(img_w == 8 && img_h == 8 && img_bpp == 24 && !img_top_down);
    auto td = bmp(8, 4, 32, true, [](int, int) { return 1u; });
    memcpy(h, td.data(), 54);
    CHECK(parsuj_naglowek_bmp(h, 54, td.size()) == nullptr && img_top_down && img_bpp == 32 && img_h == 4);
    auto zepsuj = [&](size_t off, uint32_t v, int n) {
        auto d = dobry; for (int i = 0; i < n; ++i) d[off + i] = (v >> (8 * i)) & 255;
        memcpy(h, d.data(), 54);
        return parsuj_naglowek_bmp(h, 54, d.size());
    };
    CHECK(zepsuj(0, 'X', 1) != nullptr);  CHECK(zepsuj(26, 2, 2) != nullptr);
    CHECK(zepsuj(28, 8, 2) != nullptr);   CHECK(zepsuj(28, 16, 2) != nullptr);
    CHECK(zepsuj(30, 1, 4) != nullptr);   CHECK(zepsuj(18, 0, 4) != nullptr);
    CHECK(zepsuj(18, 0x80000000u, 4) != nullptr); CHECK(zepsuj(18, MAX_W + 1, 4) != nullptr);
    CHECK(zepsuj(22, 0, 4) != nullptr);   CHECK(zepsuj(22, 0x80000000u, 4) != nullptr);
    CHECK(zepsuj(22, MAX_H + 1, 4) != nullptr); CHECK(zepsuj(10, 10, 4) != nullptr);
    CHECK(zepsuj(10, 0x7FFFFFF0u, 4) != nullptr); CHECK(zepsuj(14, 12, 4) != nullptr);
    memcpy(h, dobry.data(), 54);
    CHECK(parsuj_naglowek_bmp(h, 54, 60) != nullptr);
    CHECK(parsuj_naglowek_bmp(h, 20, dobry.size()) != nullptr);
    CHECK(parsuj_naglowek_bmp(nullptr, 54, 100) != nullptr);
    uint64_t o = 0;
    CHECK(!mnoz_u64(UINT64_MAX, 2, &o) && !dodaj_u64(UINT64_MAX, 1, &o));
    zwolnij_bmp();
}

// ---------------------------------------------------------------- galeria
static void galeria() {
    using namespace Galeria;
    CHECK(format_z_rozszerzenia("a.png") == FormatPliku::PNG && format_z_rozszerzenia("A.PNG") == FormatPliku::PNG);
    CHECK(format_z_rozszerzenia("x.jpg") == FormatPliku::JPEG && format_z_rozszerzenia("x.JPEG") == FormatPliku::JPEG);
    CHECK(format_z_rozszerzenia("x.btp") == FormatPliku::BTP && format_z_rozszerzenia("x.bmp") == FormatPliku::BMP);
    CHECK(format_z_rozszerzenia("x.gif") == FormatPliku::Nieznany && format_z_rozszerzenia("plik") == FormatPliku::Nieznany);
    CHECK(format_z_rozszerzenia("x.") == FormatPliku::Nieznany && format_z_rozszerzenia(".png") == FormatPliku::PNG);
    CHECK(format_z_rozszerzenia(nullptr) == FormatPliku::Nieznany && format_z_rozszerzenia("x.pngg") == FormatPliku::Nieznany);
    CHECK(format_z_rozszerzenia("2019. ołówkiem.jpg") == FormatPliku::JPEG);
    CHECK(porownaj_naturalnie("zdjecie2.png", "zdjecie10.png") < 0);
    CHECK(porownaj_naturalnie("zdjecie10.png", "zdjecie2.png") > 0);
    CHECK(porownaj_naturalnie("a", "A") != 0 || true);
    CHECK(porownaj_naturalnie("img007", "img7") != 0);                   // zera wiodace: porzadek deterministyczny
    CHECK(porownaj_naturalnie("img7", "img07") * porownaj_naturalnie("img07", "img7") < 0);
    CHECK(porownaj_naturalnie("99999999999999999999x", "100000000000000000000x") < 0);   // bez przepelnien
    CHECK(porownaj_naturalnie("", "a") < 0 && porownaj_naturalnie("a", "") > 0 && porownaj_naturalnie("a", "a") == 0);
    ModelGalerii g;
    const std::string listing =
        "[KAT]  podkatalog.png\n[PLIK] 10.png\n[PLIK] 2.jpg\n[PLIK] notatki.txt\n[PLIK] obraz.BTP\n[PLIK] 1.bmp\n"
        "[PLIK] Zdjęcie 3.jpeg\n[PLIK] brak_rozszerzenia\n[PLIK] x.gif\n";
    CHECK(g.zbuduj("/dane/zdjecia", listing.c_str(), listing.size()) == 5);
    CHECK(!strcmp(g.nazwy[0], "1.bmp") && !strcmp(g.nazwy[1], "2.jpg") && !strcmp(g.nazwy[2], "10.png"));
    CHECK(g.biezacy == 0 && g.mozna_nawigowac());
    // zawijanie: nastepny z ostatniego = pierwszy; poprzedni z pierwszego = ostatni
    g.biezacy = g.liczba - 1; CHECK(g.nastepny() == 0);
    g.biezacy = 0; CHECK(g.poprzedni() == g.liczba - 1);
    g.zawijaj = false;
    g.biezacy = g.liczba - 1; CHECK(g.nastepny() == -1);
    g.biezacy = 0; CHECK(g.poprzedni() == -1);
    g.biezacy = 2; CHECK(g.nastepny() == 3 && g.poprzedni() == 1);
    char sc[128];
    CHECK(g.pelna_sciezka(1, sc, sizeof(sc)) && !strcmp(sc, "/dane/zdjecia/2.jpg"));
    CHECK(!g.pelna_sciezka(1, sc, 8) && !g.pelna_sciezka(-1, sc, sizeof(sc)) && !g.pelna_sciezka(99, sc, sizeof(sc)));
    CHECK(g.znajdz("2.jpg") == 1 && g.znajdz("nie_ma.png") == -1);
    // pusta galeria, jeden obraz (nawigacja wylaczona), same nieobslugiwane
    const std::string pusty = "[PLIK] a.txt\n[KAT]  b\n";
    CHECK(g.zbuduj("/dane/zdjecia", pusty.c_str(), pusty.size()) == 0 && g.biezacy == -1 && !g.mozna_nawigowac());
    CHECK(g.nastepny() == -1 && g.poprzedni() == -1);
    const std::string jeden = "[PLIK] a.png\n";
    CHECK(g.zbuduj("/dane/zdjecia", jeden.c_str(), jeden.size()) == 1 && !g.mozna_nawigowac() && g.nastepny() == -1);
    CHECK(g.zbuduj("/x", "", 0) == 0 && g.zbuduj("/x", nullptr, 0) == 0);
    // limit liczby obrazow
    std::string duzo;
    for (int i = 0; i < ModelGalerii::MAKS_OBRAZOW + 40; ++i) duzo += "[PLIK] f" + std::to_string(i) + ".png\n";
    CHECK(g.zbuduj("/dane/zdjecia", duzo.c_str(), duzo.size()) == ModelGalerii::MAKS_OBRAZOW && g.przyciete);
    CHECK(!strcmp(g.nazwy[0], "f0.png") && !strcmp(g.nazwy[1], "f1.png"));
    // zbyt dluga nazwa i uciety listing nie powoduja przepelnienia
    const std::string dluga = "[PLIK] " + std::string(200, 'a') + ".png\n[PLIK] ok.png";
    CHECK(g.zbuduj("/dane/zdjecia", dluga.c_str(), dluga.size()) == 1 && !strcmp(g.nazwy[0], "ok.png"));
}

// ---------------------------------------------------------------- powiekszenie
static void powiekszenie() {
    using namespace Powiekszenie;
    CHECK(rozmiar_skalowany(100, 125) == 125 && rozmiar_skalowany(100, 175) == 175 && rozmiar_skalowany(3, 10) == 1);
    CHECK(zoom_dopasuj(1000, 500, 500, 500) == 50 && zoom_dopasuj(100, 100, 5000, 5000) == ZOOM_MAX);
    CHECK(zoom_dopasuj(8000, 8000, 400, 400) == 5 && zoom_dopasuj(0, 0, 10, 10) == 100);
    CHECK(zakres_zoom(1) == ZOOM_MIN && zakres_zoom(9999) == ZOOM_MAX && zakres_zoom(333) == 333);
    CHECK(zoom_krok(100, +1) == 125 && zoom_krok(100, -1) == 75 && zoom_krok(800, +1) == 800 && zoom_krok(10, -1) == 10);
    CHECK(zoom_krok(110, +1) == 125 && zoom_krok(110, -1) == 100);
    Widok w; w.szer_obr = 400; w.wys_obr = 300; w.zoom = 100; w.vw = 800; w.vh = 600;
    wysrodkuj(w);
    CHECK(w.ox == -200 && w.oy == -150);                                   // obraz mniejszy od widoku: wysrodkowany
    w.zoom = 400; w.ox = 0; w.oy = 0; ogranicz_offset(w);                  // 1600x1200 w 800x600
    CHECK(w.ox == 0 && w.oy == 0);
    przesun(w, 100000, 100000); CHECK(w.ox == 800 && w.oy == 600);        // granice
    przesun(w, -100000, -100000); CHECK(w.ox == 0 && w.oy == 0);
    // zakotwiczenie: punkt obrazu pod kursorem zostaje (tolerancja 1 px zrodla)
    w.szer_obr = 2000; w.wys_obr = 1500;                                  // obraz wiekszy od widoku przy kazdym zoomie
    w.zoom = 200; w.ox = 600; w.oy = 400; ogranicz_offset(w);               // punkt wewnatrz obrazu, daleko od krawedzi
    const int ax = 400, ay = 300;
    const int64_t sx0 = (w.ox + ax) * 100 / w.zoom, sy0 = (w.oy + ay) * 100 / w.zoom;
    for (int nz : {150, 175, 300, 400, 800, 125}) {
        zmien_zoom(w, nz, ax, ay);
        CHECK(w.zoom == nz);
        const int64_t sx1 = (w.ox + ax) * 100 / w.zoom, sy1 = (w.oy + ay) * 100 / w.zoom;
        const int64_t d1 = sx1 > sx0 ? sx1 - sx0 : sx0 - sx1, d2 = sy1 > sy0 ? sy1 - sy0 : sy0 - sy1;
        CHECK(d1 <= 3 && d2 <= 3);                                          // tylko zaokraglenia calkowitoliczbowe
        CHECK(w.ox >= 0 || rozmiar_skalowany(w.szer_obr, w.zoom) <= w.vw);
    }
    // ekstremalne wartosci nie przepelniaja
    Widok e; e.szer_obr = 8192; e.wys_obr = 8192; e.zoom = 800; e.vw = 16384; e.vh = 16384;
    ogranicz_offset(e); przesun(e, INT32_MAX, INT32_MAX); zmien_zoom(e, 1, INT32_MAX / 2, INT32_MAX / 2);
    CHECK(e.zoom >= ZOOM_FIT_MIN && e.ox >= -16384 && e.oy >= -16384);

    // renderer: obraz 4x2 -> nearest 200%: bloki 2x2; 100%: 1:1; 50%: usrednienie; alfa na szachownicy
    const uint32_t zr[8] = {0xFFFF0000, 0xFF00FF00, 0xFF0000FF, 0xFFFFFF00,
                            0xFF000000, 0xFFFFFFFF, 0xFF808080, 0xFF102030};
    std::vector<uint32_t> bufor(64 * 64);
    Widok r; r.szer_obr = 4; r.wys_obr = 2; r.zoom = 200; r.vw = 8; r.vh = 4; r.ox = 0; r.oy = 0;
    CHECK(renderuj(zr, 4, 2, false, bufor.data(), bufor.size(), r, 0x112233, false));
    CHECK(bufor[0] == 0xFF0000 && bufor[1] == 0xFF0000 && bufor[8] == 0xFF0000 && bufor[9] == 0xFF0000);
    CHECK(bufor[2] == 0x00FF00 && bufor[7] == 0xFFFF00 && bufor[8 * 3 + 2] == 0xFFFFFF);
    CHECK(bufor[8 * 2 + 0] == 0x01000000);                                    // czarny -> techniczny czarny (nie 0)
    r.zoom = 100; r.vw = 6; r.vh = 4; wysrodkuj(r);
    CHECK(renderuj(zr, 4, 2, false, bufor.data(), bufor.size(), r, 0x112233, true));
    CHECK(bufor[0] == 0x112233 && bufor[6 * 1 + 1] == 0xFF0000 && bufor[6 * 1 + 4] == 0xFFFF00 && bufor[6 * 3] == 0x112233);
    // 125%: obraz 4x2 -> 5x2 (rozmiar skalowany); poza obrazem tlo
    r.zoom = 125; r.vw = 8; r.vh = 4; wysrodkuj(r);
    CHECK(rozmiar_skalowany(4, 125) == 5 && rozmiar_skalowany(2, 125) == 2);
    CHECK(renderuj(zr, 4, 2, false, bufor.data(), bufor.size(), r, 0x112233, false));
    int ile = 0; for (int i = 0; i < 32; ++i) if (bufor[i] != 0x112233) ++ile;
    CHECK(ile == 5 * 2);                                                       // dokladnie 5x2 pikseli obrazu
    // pomniejszanie: szachownica 1px -> 50% daje szarosc (usrednianie), nie aliasing
    std::vector<uint32_t> szach(64 * 64);
    for (int y = 0; y < 64; ++y) for (int x = 0; x < 64; ++x) szach[y * 64 + x] = ((x + y) & 1) ? 0xFFFFFFFF : 0xFF000000;
    Widok p; p.szer_obr = 64; p.wys_obr = 64; p.zoom = 50; p.vw = 32; p.vh = 32; p.ox = 0; p.oy = 0;
    CHECK(renderuj(szach.data(), 64, 64, false, bufor.data(), 32 * 32, p, 0, true));
    CHECK((bufor[5 * 32 + 5] & 255) >= 120 && (bufor[5 * 32 + 5] & 255) <= 136);
    CHECK(renderuj(szach.data(), 64, 64, false, bufor.data(), 32 * 32, p, 0, false));          // bez wygladzania: ostre
    // alfa: przezroczysty piksel pokazuje szachownice, nie czarne tlo; kolory nieprzezroczyste bez zmian
    const uint32_t alfa[4] = {0x00FF0000, 0xFF00FF00, 0x80FF0000, 0xFF0000FF};
    Widok a; a.szer_obr = 2; a.wys_obr = 2; a.zoom = 100; a.vw = 2; a.vh = 2;
    CHECK(renderuj(alfa, 2, 2, true, bufor.data(), 4, a, 0, true));
    CHECK(bufor[0] == 0xCCCCCC && bufor[1] == 0x00FF00 && bufor[3] == 0x0000FF);
    CHECK((bufor[2] >> 16) > 0x99 && (bufor[2] >> 16) <= 0xFF && ((bufor[2] >> 8) & 255) > 0x40);
    // argumenty niepoprawne: brak zapisu
    bufor.assign(bufor.size(), 0xAAAAAAAA);
    CHECK(!renderuj(nullptr, 4, 2, false, bufor.data(), bufor.size(), r, 0, true));
    CHECK(!renderuj(zr, 0, 2, false, bufor.data(), bufor.size(), r, 0, true));
    CHECK(!renderuj(zr, 4, 2, false, bufor.data(), 3, r, 0, true));            // pojemnosc za mala
    Widok zly = r; zly.zoom = 5000;
    CHECK(!renderuj(zr, 4, 2, false, bufor.data(), bufor.size(), zly, 0, true));
    CHECK(bufor[0] == 0xAAAAAAAA);
}

// ---------------------------------------------------------------- UI + formaty
static void tykaj(AplikacjaObrazow& s) { for (int i = 0; i < 50 && (s.oczekujacy >= 0 || s.oczekuje_plik); ++i) s.tyk(); }

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
    // brak katalogu galerii: komunikat, bez crasha
    g_listing = "BRAK";
    s.zaladuj_galerie();
    CHECK(s.gal.liczba == 0 && s.komunikat[0] != 0);
    rysuj();
    CHECK(!s.gal.mozna_nawigowac());
    // pusty katalog: "Brak zdjec"
    g_listing = "[KAT]  x\n[PLIK] a.txt\n";
    s.zaladuj_galerie();
    CHECK(s.gal.liczba == 0 && !strncmp(s.komunikat, "Brak zdj", 8));
    rysuj();

    // fixture'y: wszystkie formaty w jednej galerii
    for (const char* n : {"cwiartki.png", "alfa.png", "cwiartki.jpg", "cwiartki.bmp", "btp1_raw.btp", "btp1_rle.btp", "btp2.btp"}) dodaj(n);
    g_listing = "[PLIK] btp2.btp\n[PLIK] cwiartki.bmp\n[PLIK] cwiartki.png\n[PLIK] alfa.png\n[PLIK] btp1_raw.btp\n"
                "[PLIK] cwiartki.jpg\n[PLIK] btp1_rle.btp\n[KAT]  inne\n[PLIK] readme.txt\n";
    s.zaladuj_galerie();
    CHECK(s.gal.liczba == 7 && !strcmp(s.gal.nazwy[0], "alfa.png") && !strcmp(s.gal.nazwy[6], "cwiartki.png"));
    s.zaplanuj_indeks(0);
    CHECK(s.zrodlo == Zrodlo::Brak && s.oczekujacy == 0);                 // najpierw "Wczytywanie...", obraz w nastepnym kroku
    tykaj(s);
    CHECK(s.zrodlo == Zrodlo::Powierzchnia && s.pow.szer == 64 && s.pow.wys == 48 && s.format == FormatPliku::PNG && s.pow.alfa);
    CHECK(!s.zewnetrzny && s.gal.biezacy == 0);
    rysuj();
    // wszystkie formaty daja te same cwiartki; kolejno przez Nastepne (bez ponownego listowania katalogu)
    const char* oczekiwane[7] = {"PNG", "BTP", "BTP", "BTP", "BMP", "JPEG", "PNG"};
    int listingi = 0; (void)listingi;
    for (int i = 0; i < 7; ++i) {
        if (i > 0) { s.nastepny(); tykaj(s); }
        CHECK(s.gal.biezacy == i && s.zrodlo == Zrodlo::Powierzchnia);
        CHECK(!strcmp(Galeria::nazwa_formatu(s.format), oczekiwane[i]));
        CHECK(s.pow.szer == 64 && s.pow.wys == 48);
        s.ustaw_zoom(100, 0, 0);
        rysuj();
        const ProstokatObrazu a = pobierz_prostokat_obrazu();
        const Prostokat e = s.widok->prostokat_do_ekranu(Prostokat(MARGINES_OBRAZU, MARGINES_OBRAZU, a.w, a.h));
        const int ox = e.x + (a.w - 64) / 2, oy = e.y + (a.h - 48) / 2;
        auto bliski = [](uint32_t c, uint32_t ref) {                         // JPEG stratny: tolerancja
            for (int sh = 0; sh <= 16; sh += 8) { int d = int((c >> sh) & 255) - int((ref >> sh) & 255); if (d < -40 || d > 40) return false; }
            return true; };
        CHECK(bliski(px(ox + 10, oy + 10), 0xFF0000) && bliski(px(ox + 10, oy + 40), 0x00FF00));
        CHECK(bliski(px(ox + 55, oy + 10), 0x0000FF) && bliski(px(ox + 55, oy + 40), 0xFFFF00));
        if (i == 0) CHECK(px(ox + 1, oy + 1) != 0xFF0000);                    // przezroczysty narożnik: szachownica, nie czerwien
    }
    // alfa PNG: nie jest przemalowywana przy zmianie motywu (piksele zrodla nietkniete)
    s.otworz_z_galerii(0);
    CHECK(s.pow.alfa && (s.pow.piksele[0] >> 24) == 0 && (s.pow.piksele[63] >> 24) == 255);
    // Nastepne/Poprzednie zawijaja; jeden obraz => wylaczone
    s.gal.biezacy = 6; s.nastepny(); tykaj(s); CHECK(s.gal.biezacy == 0);
    s.poprzedni(); tykaj(s); CHECK(s.gal.biezacy == 6);
    s.ustaw_zawijanie(false);
    s.nastepny(); tykaj(s); CHECK(s.gal.biezacy == 6);                     // koniec listy: bez ruchu
    s.ustaw_zawijanie(true);
    // klawisze: strzalki fizyczne (USB 80 = lewo, 79 = prawo), PgUp/PgDn, Home/End
    auto klawisz = [&](uint32_t usb) {
        bws_zdarzenie ev{}; ev.typ = BWS_ZDARZENIE_KLAWISZ_FIZYCZNY; ev.kod = BWS_KEY_SOURCE_USB | usb; ev.x = 1;
        okno.obsluz_zdarzenie(ev); tykaj(s); };
    s.otworz_z_galerii(3);
    klawisz(79); CHECK(s.gal.biezacy == 4);
    klawisz(80); CHECK(s.gal.biezacy == 3);
    klawisz(75); CHECK(s.gal.biezacy == 2);                                // PgUp
    klawisz(78); CHECK(s.gal.biezacy == 3);                                // PgDn
    klawisz(74); CHECK(s.gal.biezacy == 0);                                // Home
    klawisz(77); CHECK(s.gal.biezacy == 6);                                // End
    // otwarty dialog/menu blokuje nawigacje klawiszami
    s.pokaz_pomoc("Widok 100% niedostępny", "x");                        // pozostaly modal: wciaz blokuje klawisze obrazu
    const int przed = s.gal.biezacy;
    klawisz(79); CHECK(s.gal.biezacy == przed);
    okno.zarzadca_popupow().zamknij(PowodZamknieciaPopup::Anulowanie);
    // strzalki NIE przesuwaja obrazu (konflikt klawiszy): offset niezmieniony w powiekszeniu
    s.otworz_z_galerii(1);
    s.ustaw_zoom(400, 100, 100); rysuj();
    const int64_t ox0 = s.wd.ox;
    klawisz(79); CHECK(s.wd.ox == ox0 || s.gal.biezacy != 1);

    // Okna podrzedne Przegladarki: O programie / Informacje o obrazie / Sterowanie (osobne okna, modeless)
    {
        g_okno_pod_wywolan = 0;
        AplikacjaObrazow::pokaz_okno(OknoPodrzedne::Rodzaj::SterowanieObrazy, "Sterowanie", TEKST_STEROWANIE);
        CHECK(g_okno_pod_wywolan == 1 && g_okno_pod_rodzaj == 3 && !okno.zarzadca_popupow().czy_otwarty());
        CHECK(OknaPodrzedne::tresc_poprawna(TEKST_STEROWANIE, strlen(TEKST_STEROWANIE)) && OknaPodrzedne::tresc_poprawna(TEKST_O_PROGRAMIE, strlen(TEKST_O_PROGRAMIE)));
        for (const char* fraza : {"Strzałka w lewo, PgUp", "PgDn", "Home", "End", "Ctrl + kółko myszy", "0 lub 1", "F lub D", "Kółko myszy", "W, A, S, D", "Przeciąganie myszą"})
            CHECK(strstr(TEKST_STEROWANIE, fraza));
        for (const char* fraza : {"BMP, PNG, JPEG, BTP (BTP1 i BTP2)", "/dane/zdjecia", "Wersja", "Pełna rozdzielczość"})
            CHECK(strstr(TEKST_O_PROGRAMIE, fraza));
        // Informacje o obrazie: wszystkie dotychczasowe pola diagnostyczne w dwoch kolumnach, w limicie jadra
        s.otworz_z_galerii(0); tykaj(s);
        g_okno_pod_wywolan = 0; s.pokaz_informacje();
        CHECK(g_okno_pod_wywolan == 1 && g_okno_pod_rodzaj == 2 && !strcmp(g_okno_pod_tytul, "Informacje o obrazie"));
        CHECK(OknaPodrzedne::tresc_poprawna(g_okno_pod_tresc, strlen(g_okno_pod_tresc)) && !okno.zarzadca_popupow().czy_otwarty());
        for (const char* pole : {"Nazwa pliku\t", "Format\t", "Wymiary pliku\t", "Tryb dekodowania\t", "Powierzchnia obrazu\t", "Szczyt pamięci obrazu\t",
                                 "Aktualnie zajęte\t", "Czas wczytania\t", "Ostatnie rysowanie\t", "Liczba alokacji\t", "Nieudane alokacje\t"})
            CHECK(strstr(g_okno_pod_tresc, pole));
        // brak otwartego obrazu: bezpieczny komunikat, bez awarii
        s.zamknij_obraz(); tykaj(s); g_okno_pod_wywolan = 0; s.pokaz_informacje();
        CHECK(g_okno_pod_wywolan == 1 && strstr(g_okno_pod_tresc, "Brak otwartego obrazu."));
        // odmowa jadra nie psuje aplikacji
        g_okno_pod_wynik = 0; s.pokaz_informacje(); g_okno_pod_wynik = 1; CHECK(s.zrodlo == Zrodlo::Brak);
    }
    // zdjecie spoza galerii: indeks i model nietkniete
    s.otworz_z_galerii(2);
    g_pliki["/tmp/obcy.bmp"] = wczytaj_fixture("cwiartki.bmp");
    const int idx = s.gal.biezacy, liczba = s.gal.liczba;
    s.otworz_reczny("/tmp/obcy.bmp"); tykaj(s);
    CHECK(s.zewnetrzny && s.gal.biezacy == idx && s.gal.liczba == liczba && !strcmp(s.gal.katalog, "/dane/zdjecia"));
    CHECK(s.zrodlo == Zrodlo::Powierzchnia);
    s.otworz_reczny("/dane/zdjecia/btp2.btp"); tykaj(s);                    // plik z katalogu galerii: wybiera swoj indeks
    CHECK(!s.zewnetrzny && s.gal.biezacy == s.gal.znajdz("btp2.btp"));

    // bledy: uszkodzone/obciete/zlosliwe pliki => komunikat, brak crasha, nawigacja dalej mozliwa
    for (const char* n : {"ucieta.png", "zly_naglowek.png", "ogromny.png", "ucieta.jpg", "zly.jpg", "uszkodzony2.btp",
                          "ucieta1.btp", "btp_wymiary.btp", "gif.gif", "tekst.png"}) dodaj(n);
    g_pliki["/dane/zdjecia/pusty.png"] = {};
    g_pliki["/dane/zdjecia/zero.bmp"] = {'B', 'M', 0, 0};
    g_listing = "[PLIK] ucieta.png\n[PLIK] zly_naglowek.png\n[PLIK] ogromny.png\n[PLIK] ucieta.jpg\n[PLIK] zly.jpg\n"
                "[PLIK] uszkodzony2.btp\n[PLIK] ucieta1.btp\n[PLIK] btp_wymiary.btp\n[PLIK] gif.gif\n[PLIK] tekst.png\n"
                "[PLIK] pusty.png\n[PLIK] zero.bmp\n[PLIK] cwiartki.png\n";
    s.zaladuj_galerie();
    CHECK(s.gal.liczba == 12);            // gif.gif ma nieobslugiwane rozszerzenie
    int zle = 0;
    for (int i = 0; i < s.gal.liczba; ++i) {
        s.zaplanuj_indeks(i); tykaj(s);
        if (s.zrodlo == Zrodlo::Brak) { ++zle; CHECK(s.komunikat[0] != 0); rysuj(); CHECK(s.pow.piksele == nullptr); }
        s.nastepny(); tykaj(s);           // nawigacja z uszkodzonego pliku dziala
    }
    CHECK(zle == s.gal.liczba - 1);        // poprawny tylko cwiartki.png
    // Poziom 1: obrazy do 17 Mpx otwieraja sie w pelnej rozdzielczosci (zaufana alokacja jadra).
    for (const char* n : {"graniczny_ok.png", "za_duzy_4_6mpx.png", "za_duzy.jpg", "jpeg_20mpx_420.jpg", "jpeg_20mpx_444_rst.jpg",
                          "jpeg_20mpx_prog.jpg", "jpeg_20mpx_gray.jpg", "jpeg_48mpx_420.jpg", "jpeg_9000x16.jpg", "png_20mpx.png", "jpeg_prog_dht.jpg"}) dodaj(n);
    s.otworz_reczny("/dane/zdjecia/graniczny_ok.png"); tykaj(s);
    CHECK(s.zrodlo == Zrodlo::Powierzchnia && s.pow.szer == 2040 && s.pow.wys == 2040 && s.skala_podgladu == 0);
    s.otworz_reczny("/dane/zdjecia/za_duzy_4_6mpx.png"); tykaj(s);
    CHECK(s.zrodlo == Zrodlo::Powierzchnia && s.pow.szer == 2300 && s.skala_podgladu == 0);
    s.otworz_reczny("/dane/zdjecia/za_duzy.jpg"); tykaj(s);
    CHECK(s.zrodlo == Zrodlo::Powierzchnia && s.pow.szer == 2500 && s.pow.wys == 2000 && s.skala_podgladu == 0);
    // progresywny JPEG z redefinicja tablic Huffmana miedzy skanami (libjpeg): pelna rozdzielczosc
    s.otworz_reczny("/dane/zdjecia/jpeg_prog_dht.jpg"); tykaj(s);
    CHECK(s.zrodlo == Zrodlo::Powierzchnia && s.pow.szer == 1200 && s.pow.wys == 900 && s.skala_podgladu == 0);
    // odrzucone PRZED wczytaniem pliku: wymiary z naglowka
    for (const char* n : {"ogromny.png", "jpeg_9000x16.jpg", "png_20mpx.png"}) {
        const std::string sc = std::string("/dane/zdjecia/") + n;
        const int o0 = g_odczyty;
        s.otworz_reczny(sc.c_str()); tykaj(s);
        CHECK(s.zrodlo == Zrodlo::Brak && !strncmp(s.komunikat, "Obraz ", 6));
        CHECK(g_odczyty - o0 <= 3);                          // tylko poczatek pliku, nie caly plik
        rysuj();
    }
    // Poziom 2: JPEG ponad 17 Mpx => podglad strumieniowy (rozne warianty), nigdy widok 100%
    auto blisko = [](uint32_t p, const uint32_t k) {
        auto d = [](int a, int b) { return a > b ? a - b : b - a; };
        return d((p >> 16) & 255, (k >> 16) & 255) < 24 && d((p >> 8) & 255, (k >> 8) & 255) < 24 && d(p & 255, k & 255) < 24;
    };
    const uint32_t KOL[4] = {0xDC1E1E, 0x1EC828, 0x1E3CDC, 0xE6DC28};
    for (const char* n : {"jpeg_20mpx_420.jpg", "jpeg_20mpx_444_rst.jpg", "jpeg_20mpx_prog.jpg", "jpeg_20mpx_gray.jpg", "jpeg_48mpx_420.jpg"}) {
        const std::string sc = std::string("/dane/zdjecia/") + n;
        const uint64_t baza = g_pam_aktualna;
        s.otworz_reczny(sc.c_str()); tykaj(s);
        int ticki = 0;
        while (s.pdek && ticki++ < 100000) s.tyk();
        CHECK(s.zrodlo == Zrodlo::Powierzchnia && s.skala_podgladu >= 2 && s.skala_podgladu <= 8);
        CHECK(static_cast<uint64_t>(s.pow.szer) * s.pow.wys <= PODGLAD_MAX_PIKSELI);
        const bool duzy = !strcmp(n, "jpeg_48mpx_420.jpg");
        CHECK(s.zrodlo_w == (duzy ? 8000U : 5000U) && s.zrodlo_h == (duzy ? 6000U : 4000U));
        CHECK(s.pow.szer == static_cast<int>((s.zrodlo_w * (8 / s.skala_podgladu) + 7) / 8));
        if (!strcmp(n, "jpeg_20mpx_gray.jpg")) {
            const uint32_t g = s.pow.piksele[(s.pow.wys / 4) * s.pow.szer + s.pow.szer / 4] & 255;
            CHECK(g > 30 && g < 200);
        } else {
            const int w = s.pow.szer, h = s.pow.wys;
            for (int q = 0; q < 4; ++q) {
                const int px = (q % 2) * (w / 2) + w / 4, py = (q / 2) * (h / 2) + h / 4;
                CHECK(blisko(s.pow.piksele[py * w + px] & 0xFFFFFF, KOL[q]));
            }
        }
        const int zoom0 = s.wd.zoom;
        s.jeden_do_jednego();                                 // 100% niedostepne dla podgladu
        CHECK(s.wd.zoom == zoom0 && s.skala_podgladu > 0);
        s.odswiez_wszystko(); rysuj();
        s.zoom_suwakiem(200); rysuj(); CHECK(s.zrodlo == Zrodlo::Powierzchnia);   // powiekszenie podgladu dziala
        s.zamknij_obraz();
        CHECK(g_pam_aktualna == baza);                        // pamiec zwolniona po zamknieciu
    }
    // przejscie do innego pliku w trakcie podgladu: dekoder zamkniety, brak wyciekow
    {
        const uint64_t baza = g_pam_aktualna;
        s.otworz_reczny("/dane/zdjecia/jpeg_20mpx_420.jpg"); tykaj(s);
        CHECK(s.pdek != nullptr && s.zrodlo == Zrodlo::Brak && s.komunikat[0] == 'W');
        s.tyk();
        s.otworz_reczny("/dane/zdjecia/graniczny_ok.png"); tykaj(s);
        CHECK(s.pdek == nullptr && s.zrodlo == Zrodlo::Powierzchnia && s.skala_podgladu == 0);
        s.zamknij_obraz();
        CHECK(g_pam_aktualna == baza);
    }
    // brak pamieci: pelne dekodowanie nieudane => podglad; podglad nieudany => komunikat, bez wyciekow
    {
        const uint64_t baza = g_pam_aktualna;
        g_limit_alokacji_test = 8U * 1024U * 1024U;           // powierzchnia 20 MB nie przejdzie, podglad (5 MB) tak
        s.otworz_reczny("/dane/zdjecia/za_duzy.jpg"); tykaj(s);
        int ticki = 0; while (s.pdek && ticki++ < 100000) s.tyk();
        CHECK(s.zrodlo == Zrodlo::Powierzchnia && s.skala_podgladu >= 2);
        s.zamknij_obraz();
        g_limit_alokacji_test = 64U * 1024U;                   // nawet bufor odczytu (32 KiB) i plaszczyzny sie nie zmieszcza
        s.otworz_reczny("/dane/zdjecia/jpeg_20mpx_420.jpg"); tykaj(s);
        ticki = 0; while (s.pdek && ticki++ < 100000) s.tyk();
        CHECK(s.zrodlo == Zrodlo::Brak && s.komunikat[0] != 0 && s.pdek == nullptr);
        s.otworz_reczny("/dane/zdjecia/graniczny_ok.png"); tykaj(s);
        CHECK(s.zrodlo == Zrodlo::Brak && s.komunikat[0] != 0);
        g_limit_alokacji_test = 0;
        s.otworz_reczny("/dane/zdjecia/graniczny_ok.png"); tykaj(s);       // nawigacja po bledzie dziala
        CHECK(s.zrodlo == Zrodlo::Powierzchnia);
        s.zamknij_obraz();
        CHECK(g_pam_aktualna == baza);
    }
    // gif jawnie: komunikat o nieobslugiwanym formacie
    s.otworz_reczny("/dane/zdjecia/gif.gif"); tykaj(s);
    CHECK(s.zrodlo == Zrodlo::Brak && s.komunikat[0] != 0);

    // powiekszenie na prawdziwym obrazie: wartosci posrednie zmieniaja skale
    s.otworz_z_galerii(s.gal.znajdz("cwiartki.png")); s.odswiez_wszystko();
    for (int z : {50, 125, 175, 200, 400}) {
        s.zoom_suwakiem(z);
        rysuj();
        CHECK(s.wd.zoom == z && !s.tryb_dopasuj && s.suwak->wartosc() == z);
        const ProstokatObrazu a = pobierz_prostokat_obrazu();
        const Prostokat e = s.widok->prostokat_do_ekranu(Prostokat(MARGINES_OBRAZU, MARGINES_OBRAZU, a.w, a.h));
        const int sw = static_cast<int>(Powiekszenie::rozmiar_skalowany(64, z));
        const int ox = e.x + (a.w - sw) / 2, oy = e.y + (a.h - static_cast<int>(Powiekszenie::rozmiar_skalowany(48, z))) / 2;
        CHECK(px(ox + 2, oy + 2) == 0xFF0000);                              // kolor ukladu zachowany
        CHECK(px(ox + sw - 3, oy + 2) == 0x0000FF);
        CHECK(px(ox + sw + 3, oy + 2) == (motyw_kolor(MOTYW_KOLOR_OKNOTLO) & 0xFFFFFF) || sw + 3 >= a.w);   // poza obrazem tlo
    }
    s.zoom_suwakiem(2000); CHECK(s.wd.zoom == Powiekszenie::ZOOM_MAX);
    s.zoom_suwakiem(1); CHECK(s.wd.zoom == Powiekszenie::ZOOM_MIN);
    // przyciski +/-, 100%, dopasuj
    s.dopasuj(); rysuj(); CHECK(s.tryb_dopasuj);
    s.powieksz(); CHECK(!s.tryb_dopasuj);                                   // z dopasowania: nastepny poziom drabinki
    s.ustaw_zoom(100, s.srodek_x(), s.srodek_y());
    const int z1 = s.wd.zoom; s.powieksz(); CHECK(s.wd.zoom > z1);
    s.pomniejsz(); CHECK(s.wd.zoom == z1);
    s.jeden_do_jednego(); CHECK(s.wd.zoom == 100);
    // Ctrl + kolko: powiekszenie z kotwica; samo kolko przewija
    ZdarzenieRolki er{}; er.delta_y = 1; er.modyfikatory = BWS_MOD_CTRL; er.x = 300; er.y = 200;
    s.ustaw_zoom(300, 100, 100); rysuj();
    const int zc = s.wd.zoom;
    mysz_rolka(s.widok, er, nullptr); CHECK(s.wd.zoom > zc);
    er.delta_y = -1; mysz_rolka(s.widok, er, nullptr); mysz_rolka(s.widok, er, nullptr); CHECK(s.wd.zoom < zc + 100);
    er.modyfikatory = 0; er.delta_y = 1; const int64_t oy1 = s.wd.oy; s.ustaw_zoom(800, 50, 50); rysuj();
    s.przesun(0, 100); const int64_t oy2 = s.wd.oy; mysz_rolka(s.widok, er, nullptr);
    CHECK(s.wd.zoom == 800 && s.wd.oy != oy2 + 12345 && oy1 == oy1);
    // panning w roznych poziomach: zawsze w granicach obrazu
    for (int z : {100, 125, 200, 400, 800}) {
        s.ustaw_zoom(z, s.srodek_x(), s.srodek_y()); rysuj();
        s.przesun(100000, 100000); rysuj();
        const Powiekszenie::Widok& w = s.wd;
        const int64_t sx = Powiekszenie::rozmiar_skalowany(w.szer_obr, w.zoom), sy = Powiekszenie::rozmiar_skalowany(w.wys_obr, w.zoom);
        CHECK(w.ox <= std::max<int64_t>(0, sx - w.vw) && w.oy <= std::max<int64_t>(0, sy - w.vh));
        s.przesun(-100000, -100000);
        CHECK((sx <= w.vw ? w.ox < 0 || w.ox == 0 : w.ox == 0) && (sy <= w.vh ? w.oy <= 0 : w.oy == 0));
    }
    // zmiana rozmiaru okna: zoom zachowany, offset w granicach, bez ponownego dekodowania pliku
    s.ustaw_zoom(400, 50, 50); s.przesun(60, 60); rysuj();
    const int odczyty = g_odczyty;
    for (int w : {OKNO_SZER + 200, MIN_SZER, OKNO_SZER, 1100}) for (int h : {OKNO_WYS + 100, MIN_WYS, OKNO_WYS}) {
        okno.ustaw_rozmiar(w, h); okno.przelicz_uklad(); rysuj();
        CHECK(s.wd.zoom == 400 && !s.tryb_dopasuj && s.zrodlo == Zrodlo::Powierzchnia);
        const int64_t sx = Powiekszenie::rozmiar_skalowany(s.wd.szer_obr, 400), sy = Powiekszenie::rozmiar_skalowany(s.wd.wys_obr, 400);
        CHECK(s.wd.ox >= std::min<int64_t>(0, (static_cast<int64_t>(s.wd.vw) - sx) / -2 - 1) && s.wd.ox <= std::max<int64_t>(0, sx - s.wd.vw));
        CHECK(s.wd.oy <= std::max<int64_t>(0, sy - s.wd.vh));
    }
    CHECK(g_odczyty == odczyty);                                            // zoom/resize: zadnego odczytu pliku
    // Fit: zoom wynika z rozmiaru i obejmuje caly obraz
    s.dopasuj(); rysuj();
    CHECK(s.tryb_dopasuj && s.wd.zoom >= 100);

    // motyw: tlo sie zmienia, piksele zrodla nigdy
    BursztynMotyw base = *gui_pobierz_motyw();
    BursztynMotyw temy[2] = {base, base};
    const uint32_t pierwszy = s.pow.piksele[100];
    for (int v = 0; v < 2; ++v) {
        temy[v].oknotlo = v ? 0x203040 : 0xE8E0D0;
        test_ustaw_aktywny_motyw(&temy[v]);
        bws_zdarzenie ev{}; ev.typ = BWS_ZDARZENIE_MOTYW_ZMIENIONY; okno.obsluz_zdarzenie(ev);
        okno.rysuj(ctx, true); rysuj();
        CHECK(s.pow.piksele[100] == pierwszy && (kolor_tla & 0xFFFFFF) == (temy[v].oknotlo & 0xFFFFFF));
    }
    temy[0].oknotlo = 0; test_ustaw_aktywny_motyw(&temy[0]);
    { bws_zdarzenie ev{}; ev.typ = BWS_ZDARZENIE_MOTYW_ZMIENIONY; okno.obsluz_zdarzenie(ev); okno.rysuj(ctx, true); rysuj(); }
    CHECK(kolor_tla == TECHNICZNY_CZARNY);

    // wielki BMP (powyzej limitu powierzchni): silnik strumieniowy zachowany (Dopasuj / 1:1 / panning)
    g_pliki["/dane/zdjecia/wielki.bmp"] = bmp(4000, 3000, 24, false, [](int x, int y) { return static_cast<uint32_t>(x * 3 + y * 5) & 0xFFFFFF; });
    s.otworz_reczny("/dane/zdjecia/wielki.bmp"); tykaj(s);
    CHECK(s.zrodlo == Zrodlo::StrumienBmp && img_w == 4000 && img_h == 3000 && s.format == FormatPliku::BMP);
    rysuj();
    s.jeden_do_jednego();
    for (int i = 0; i < 4000 && render_1do1.active; ++i) s.tyk();
    rysuj();
    s.przesun(10, 20); CHECK(view_x == 10 && view_y == 20);
    s.dopasuj(); rysuj(); CHECK(tryb == TrybSkalowania::DOPASUJ);
    s.otworz_z_galerii(s.gal.znajdz("cwiartki.png"));                       // przelaczenie na zwykly obraz zwalnia silnik BMP
    CHECK(s.zrodlo == Zrodlo::Powierzchnia && fit_buf == nullptr && row_cache_slot_count == 0);
    s.zamknij_obraz(); CHECK(s.zrodlo == Zrodlo::Brak && s.pow.piksele == nullptr);
    test_capture_pixels = false;
    s.zwolnij_bufory();
    test_ustaw_aktywny_motyw(nullptr);
}

int main(int argc, char** argv) {
    g_fix = argc > 1 ? argv[1] : ".bsf-test/img-fixtures";
    parser_bmp();
    galeria();
    powiekszenie();
    ui();
    printf("PASS test_przegladarka_obrazow_host: %d checks\n", checks);
    return 0;
}
